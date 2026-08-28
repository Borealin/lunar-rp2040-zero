cmake_minimum_required(VERSION 3.17)

set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Target board")

if(NOT DEFINED PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH to a Pico SDK checkout")
    endif()
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(lunar_rp2040_zero C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(lunar_rp2040_zero
    src/main.c
    src/serial_protocol.c
    src/tsl2591.c
    src/usb_descriptors.c
    src/ws2812_off.c
)

pico_generate_pio_header(lunar_rp2040_zero ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(lunar_rp2040_zero PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(lunar_rp2040_zero PRIVATE
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
    PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0
)

target_compile_options(lunar_rp2040_zero PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-error=sign-conversion
    -Wno-error=conversion
)

target_link_libraries(lunar_rp2040_zero PRIVATE
    hardware_i2c
    hardware_pio
    pico_bootrom
    pico_stdlib
    pico_unique_id
    pico_usb_reset
    tinyusb_board
    tinyusb_device
)

pico_enable_stdio_usb(lunar_rp2040_zero 0)
pico_enable_stdio_uart(lunar_rp2040_zero 1)
pico_add_extra_outputs(lunar_rp2040_zero)
