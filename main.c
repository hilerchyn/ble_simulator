/**
 * Copyright (c) 2022 Andrew McDonnell
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "btstack.h"
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"
#include "pico/cyw43_driver.h"
#include "pico/stdlib.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "hardware/uart.h"

#include "Pico_UPS.h"

// UART
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5


/**
 * blue tooth
 */
#include "btstack.h"

#if 0
#define DEBUG_LOG(...) printf(__VA_ARGS__)
#else
#define DEBUG_LOG(...)
#endif

#define LED_QUICK_FLASH_DELAY_MS 100
#define LED_SLOW_FLASH_DELAY_MS 1000

typedef enum {
  TC_OFF,
  TC_IDLE,
  TC_W4_SCAN_RESULT,
  TC_W4_CONNECT,
  TC_W4_SERVICE_RESULT,
  TC_W4_CHARACTERISTIC_RESULT,
  TC_W4_ENABLE_NOTIFICATIONS_COMPLETE,
  TC_W4_READY
} gc_state_t;

static btstack_packet_callback_registration_t hci_event_callback_registration;
static gc_state_t state = TC_OFF;
static bd_addr_t server_addr;
static bd_addr_type_t server_addr_type;
static hci_con_handle_t connection_handle;
static gatt_client_service_t server_service;
static gatt_client_characteristic_t server_characteristic;
static bool listener_registered;
static gatt_client_notification_t notification_listener;
static btstack_timer_source_t heartbeat;

static void client_start(void) {
  DEBUG_LOG("Start scanning!\n");
  state = TC_W4_SCAN_RESULT;
  gap_set_scan_parameters(0, 0x0030, 0x0030);
  gap_start_scan();
}

static bool
advertisement_report_contains_service(uint16_t service,
                                      uint8_t *advertisement_report) {
  // get advertisement from report event
  const uint8_t *adv_data =
      gap_event_advertising_report_get_data(advertisement_report);
  uint8_t adv_len =
      gap_event_advertising_report_get_data_length(advertisement_report);

  // iterate over advertisement data
  ad_context_t context;
  for (ad_iterator_init(&context, adv_len, adv_data);
       ad_iterator_has_more(&context); ad_iterator_next(&context)) {
    uint8_t data_type = ad_iterator_get_data_type(&context);
    uint8_t data_size = ad_iterator_get_data_len(&context);
    const uint8_t *data = ad_iterator_get_data(&context);
    switch (data_type) {
    case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS:
      for (int i = 0; i < data_size; i += 2) {
        uint16_t type = little_endian_read_16(data, i);
        if (type == service)
          return true;
      }
    default:
      break;
    }
  }
  return false;
}

static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel,
                                     uint8_t *packet, uint16_t size) {
  UNUSED(packet_type);
  UNUSED(channel);
  UNUSED(size);

  uint8_t att_status;
  switch (state) {
  case TC_W4_SERVICE_RESULT:
    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_SERVICE_QUERY_RESULT:
      // store service (we expect only one)
      DEBUG_LOG("Storing service\n");
      gatt_event_service_query_result_get_service(packet, &server_service);
      break;
    case GATT_EVENT_QUERY_COMPLETE:
      att_status = gatt_event_query_complete_get_att_status(packet);
      if (att_status != ATT_ERROR_SUCCESS) {
        printf("SERVICE_QUERY_RESULT, ATT Error 0x%02x.\n", att_status);
        gap_disconnect(connection_handle);
        break;
      }
      // service query complete, look for characteristic
      state = TC_W4_CHARACTERISTIC_RESULT;
      DEBUG_LOG("Search for env sensing characteristic.\n");
      gatt_client_discover_characteristics_for_service_by_uuid16(
          handle_gatt_client_event, connection_handle, &server_service,
          ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE);
      break;
    default:
      break;
    }
    break;
  case TC_W4_CHARACTERISTIC_RESULT:
    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
      DEBUG_LOG("Storing characteristic\n");
      gatt_event_characteristic_query_result_get_characteristic(
          packet, &server_characteristic);
      break;
    case GATT_EVENT_QUERY_COMPLETE:
      att_status = gatt_event_query_complete_get_att_status(packet);
      if (att_status != ATT_ERROR_SUCCESS) {
        printf("CHARACTERISTIC_QUERY_RESULT, ATT Error 0x%02x.\n", att_status);
        gap_disconnect(connection_handle);
        break;
      }
      // register handler for notifications
      listener_registered = true;
      gatt_client_listen_for_characteristic_value_updates(
          &notification_listener, handle_gatt_client_event, connection_handle,
          &server_characteristic);
      // enable notifications
      DEBUG_LOG("Enable notify on characteristic.\n");
      state = TC_W4_ENABLE_NOTIFICATIONS_COMPLETE;
      gatt_client_write_client_characteristic_configuration(
          handle_gatt_client_event, connection_handle, &server_characteristic,
          GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
      break;
    default:
      break;
    }
    break;
  case TC_W4_ENABLE_NOTIFICATIONS_COMPLETE:
    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_QUERY_COMPLETE:
      DEBUG_LOG("Notifications enabled, ATT status 0x%02x\n",
                gatt_event_query_complete_get_att_status(packet));
      if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS)
        break;
      state = TC_W4_READY;
      break;
    default:
      break;
    }
    break;
  case TC_W4_READY:
    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_NOTIFICATION: {
      uint16_t value_length = gatt_event_notification_get_value_length(packet);
      const uint8_t *value = gatt_event_notification_get_value(packet);
      DEBUG_LOG("Indication value len %d\n", value_length);
      if (value_length == 2) {
        float temp = little_endian_read_16(value, 0);
        printf("read temp %.2f degc\n", temp / 100);
      } else {
        printf("Unexpected length %d\n", value_length);
      }
      break;
    }
    default:
      printf("Unknown packet type 0x%02x\n", hci_event_packet_get_type(packet));
      break;
    }
    break;
  default:
    printf("error\n");
    break;
  }
}

static void hci_event_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size) {
  UNUSED(size);
  UNUSED(channel);
  bd_addr_t local_addr;
  if (packet_type != HCI_EVENT_PACKET)
    return;

  uint8_t event_type = hci_event_packet_get_type(packet);
  switch (event_type) {
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
      gap_local_bd_addr(local_addr);
      printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));
      client_start();
    } else {
      state = TC_OFF;
    }
    break;
  case GAP_EVENT_ADVERTISING_REPORT:
    if (state != TC_W4_SCAN_RESULT)
      return;
    // check name in advertisement
    if (!advertisement_report_contains_service(
            ORG_BLUETOOTH_SERVICE_ENVIRONMENTAL_SENSING, packet))
      return;
    // store address and type
    gap_event_advertising_report_get_address(packet, server_addr);
    server_addr_type = gap_event_advertising_report_get_address_type(packet);
    // stop scanning, and connect to the device
    state = TC_W4_CONNECT;
    gap_stop_scan();
    printf("Connecting to device with addr %s.\n", bd_addr_to_str(server_addr));
    gap_connect(server_addr, server_addr_type);
    break;
  case HCI_EVENT_LE_META:
    // wait for connection complete
    switch (hci_event_le_meta_get_subevent_code(packet)) {
    case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
      if (state != TC_W4_CONNECT)
        return;
      connection_handle =
          hci_subevent_le_connection_complete_get_connection_handle(packet);
      // initialize gatt client context with handle, and add it to the list of
      // active clients query primary services
      DEBUG_LOG("Search for env sensing service.\n");
      state = TC_W4_SERVICE_RESULT;
      gatt_client_discover_primary_services_by_uuid16(
          handle_gatt_client_event, connection_handle,
          ORG_BLUETOOTH_SERVICE_ENVIRONMENTAL_SENSING);
      break;
    default:
      break;
    }
    break;
  case HCI_EVENT_DISCONNECTION_COMPLETE:
    // unregister listener
    connection_handle = HCI_CON_HANDLE_INVALID;
    if (listener_registered) {
      listener_registered = false;
      gatt_client_stop_listening_for_characteristic_value_updates(
          &notification_listener);
    }
    printf("Disconnected %s\n", bd_addr_to_str(server_addr));
    if (state == TC_OFF)
      break;
    client_start();
    break;
  default:
    break;
  }
}

static void heartbeat_handler(struct btstack_timer_source *ts) {
  // Invert the led
  static bool quick_flash;
  static bool led_on = true;

  led_on = !led_on;
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
  if (listener_registered && led_on) {
    quick_flash = !quick_flash;
  } else if (!listener_registered) {
    quick_flash = false;
  }

  // Restart timer
  btstack_run_loop_set_timer(ts, (led_on || quick_flash)
                                     ? LED_QUICK_FLASH_DELAY_MS
                                     : LED_SLOW_FLASH_DELAY_MS);
  btstack_run_loop_add_timer(ts);
}
/////////////////////////////////////////////////////////////////
//////BLUE TOOTH END/////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////

#define UDP_PORT 8080
#define BEACON_MSG_LEN_MAX 128
#define BEACON_TARGET "47.115.207.102"
#define BEACON_INTERVAL_MS 1000

void run_udp_beacon() {
  struct udp_pcb *pcb = udp_new();

  ip_addr_t addr;
  ipaddr_aton(BEACON_TARGET, &addr);


  // 电池
  INA219Handle ina = INA219_create(0x43);
  INA219_begin(ina);

  int counter = 0;
  while (true) {

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    sleep_ms(1000);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    sleep_ms(1000);

    // fetch the battary's voltage
    float voltage = INA219_getBusVoltage_V(ina);
    printf("Bus Voltage: %f V\n", voltage);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, BEACON_MSG_LEN_MAX + 1, PBUF_RAM);
    char *req = (char *)p->payload;
    memset(req, 0, BEACON_MSG_LEN_MAX + 1);
    snprintf(req, BEACON_MSG_LEN_MAX, "from ble_simulator: counter: %d voltage: %f", counter, voltage);
    err_t er = udp_sendto(pcb, p, &addr, UDP_PORT);
    pbuf_free(p);
    if (er != ERR_OK) {
      printf("Failed to send UDP packet! error=%d", er);
    } else {
      printf("Sent packet %d\n", counter);
      counter++;
    }

    // Note in practice for this simple UDP transmitter,
    // the end result for both background and poll is the same

#if PICO_CYW43_ARCH_POLL
    // if you are using pico_cyw43_arch_poll, then you must poll periodically
    // from your main loop (not from a timer) to check for Wi-Fi driver or lwIP
    // work that needs to be done.
    cyw43_arch_poll();
    sleep_ms(BEACON_INTERVAL_MS);
#else
    // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
    // is done via interrupt in the background. This sleep is just an example of
    // some (blocking) work you might be doing.
    sleep_ms(BEACON_INTERVAL_MS);
#endif

  }

  INA219_destroy(ina);
}

int main() {
  stdio_init_all();

  if (cyw43_arch_init()) {
    printf("failed to initialise\n");
    return 1;
  }

  // uart
  uart_init(UART_ID, BAUD_RATE);
  //gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  //gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  //uart_set_hw_flow(UART_ID, false, false);
  //uart_puts(UART_ID, "Debug: Hello, Pico 2 W!\n");
  printf("Debug: 1\n");

  // bluetooth
  // bluetooth

  l2cap_init();
  printf("Debug: 2\n");
  sm_init();
  printf("Debug: 3\n");
  sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  printf("Debug: 4\n");

  // setup empty ATT server - only needed if LE Peripheral does ATT queries on
  // its own, e.g. Android and iOS
  att_server_init(NULL, NULL, NULL);
  printf("Debug: 5\n");

  gatt_client_init();
  printf("Debug: 6\n");

  hci_event_callback_registration.callback = &hci_event_handler;
  printf("Debug: 7\n");
  hci_add_event_handler(&hci_event_callback_registration);
  printf("Debug: 8\n");

  // set one-shot btstack timer
  heartbeat.process = &heartbeat_handler;
  printf("Debug: 9\n");
  btstack_run_loop_set_timer(&heartbeat, LED_SLOW_FLASH_DELAY_MS);
  printf("Debug: 10\n");
  btstack_run_loop_add_timer(&heartbeat);
  printf("Debug: 11\n");

  // turn on!
  hci_power_control(HCI_POWER_ON);
  printf("Debug: 12\n");

  //btstack_run_loop_execute();
  printf("Debug: 13\n");
  // bluetooth end
  // bluetooth end

  cyw43_arch_enable_sta_mode();

  printf("Connecting to Wi-Fi...\n");
  while (true) {
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
      printf("failed to connect.\n");
      sleep_ms(2000);
      //return 1;
    } else {
      printf("Connected.\n");
      break;
    }
  }
  run_udp_beacon();
  cyw43_arch_deinit();
  return 0;
}

