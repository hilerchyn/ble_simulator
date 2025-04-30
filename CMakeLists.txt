set(PROJECT ble_simulator)

set(PICO_BOARD pico2_w)
set(WIFI_SSID "MyHome")
set(WIFI_PASSWORD "abcd.5678")
set(PICO_CYW43_SUPPORTED 1)
set(PICO_BTSTACK_SUPPORTED 1)

cmake_minimum_required(VERSION 3.12)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(${PROJECT} C CXX ASM)

pico_sdk_init()

add_executable(${PROJECT} 
	#main.cpp
	main.c
)

target_compile_definitions(${PROJECT} PRIVATE
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        )

target_link_libraries(${PROJECT}
	pico_stdlib
	pico_cyw43_arch_lwip_threadsafe_background

	pico_btstack_ble
	pico_btstack_cyw43
	pico_cyw43_arch_none
	hardware_adc
)

target_include_directories(${PROJECT} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR} # For btstack config
)

target_compile_definitions(${PROJECT} PRIVATE
        RUNNING_AS_CLIENT=1
)

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

pico_add_extra_outputs(${PROJECT})

