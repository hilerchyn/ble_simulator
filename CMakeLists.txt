set(PROJECT hello_usb_cpp)

set(PICO_CYW43_SUPPORTED 1)
set(PICO_BTSTACK_SUPPORTED 1)

cmake_minimum_required(VERSION 3.12)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(${PROJECT} C CXX ASM)

pico_sdk_init()

add_executable(${PROJECT} main.cpp)

target_link_libraries(${PROJECT}
	#pico_cyw43_arch_lwip_threadsafe_background
	pico_stdlib

	pico_btstack_ble
	pico_btstack_cyw43
	pico_cyw43_arch_none
	hardware_adc
)

target_include_directories(${PROJECT} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR} # For btstack config
)

pico_add_extra_outputs(${PROJECT})
pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

