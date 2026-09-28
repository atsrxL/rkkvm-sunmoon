# RKMoon RP2350 HID bridge firmware (docs/ADR-012-rp2350-hid-bridge.md)
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)

# Board: pico-sdk 2.3.1 has no waveshare_rp2350_usb_c header, so this project
# ships one in boards/ (written from the official schematic).
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD waveshare_rp2350_usb_c)
endif ()
if (NOT PICO_BOARD STREQUAL "waveshare_rp2350_usb_c")
    message(WARNING "PICO_BOARD=${PICO_BOARD}: pins/flash may not match the Waveshare RP2350-USB-C")
endif ()
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico platform")

option(RKMOON_PIO_USB_CDC "Enable independent PIO-USB CDC control channel" ON)
set(RKMOON_CDC_PID "0x0002" CACHE STRING "PIO CDC product ID (internal testing)")

set(RKMOON_USB_VID "0x1209" CACHE STRING "USB vendor ID")
set(RKMOON_USB_PID "0x0001" CACHE STRING "USB product ID")

include(pico_sdk_import.cmake)
project(rkmoon_rp2350_hid C CXX ASM)
pico_sdk_init()

if (PICO_SDK_VERSION_MAJOR LESS 2)
    message(FATAL_ERROR "Pico SDK 2.x required")
endif ()

add_executable(rkmoon_rp2350_hid
    src/main.c
    src/usb_descriptors.c
    src/uart_link.c
    src/status_led.c
    src/core/crc16.c
    src/core/proto.c
    src/core/bridge.c
)

pico_generate_pio_header(rkmoon_rp2350_hid ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(rkmoon_rp2350_hid PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/core
)

target_compile_definitions(rkmoon_rp2350_hid PRIVATE
    RKMOON_USB_VID=${RKMOON_USB_VID}
    RKMOON_USB_PID=${RKMOON_USB_PID}
    RKMOON_PIO_USB_CDC=$<BOOL:${RKMOON_PIO_USB_CDC}>
    RKMOON_WS2812_PIN=16
    RKMOON_UART_TX_PIN=0
    RKMOON_UART_RX_PIN=1
    RKMOON_UART_BAUD=1000000
    # No stdio: UART0 is the protocol channel, USB is HID only.
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
)

target_compile_options(rkmoon_rp2350_hid PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)

target_link_libraries(rkmoon_rp2350_hid PRIVATE
    pico_stdlib
    pico_unique_id
    hardware_uart
    hardware_pio
    hardware_watchdog
    hardware_irq
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_uart(rkmoon_rp2350_hid 0)
pico_enable_stdio_usb(rkmoon_rp2350_hid 0)
pico_set_program_name(rkmoon_rp2350_hid "RKMoon HID Bridge")
pico_set_program_version(rkmoon_rp2350_hid "0.2")
pico_add_extra_outputs(rkmoon_rp2350_hid)

if(RKMOON_PIO_USB_CDC)
    if(RKMOON_CDC_PID STREQUAL RKMOON_USB_PID)
        message(FATAL_ERROR "CDC and HID must have distinct PIDs")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(PIO_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../build/rp2350-hid-deps/Pico-PIO-USB" CACHE PATH "Pinned PIO-USB source")
    set(PIO_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/pio-usb")
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/prepare_pio.py
        ${PIO_SOURCE} ${PIO_PATCHED} RESULT_VARIABLE patch_result)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Pinned PIO-USB patch verification failed")
    endif()
    add_library(rkmoon_pio_ll STATIC ${PIO_PATCHED}/src/pio_usb.c
        ${PIO_PATCHED}/src/pio_usb_device.c ${PIO_PATCHED}/src/usb_crc.c)
    pico_generate_pio_header(rkmoon_pio_ll ${PIO_PATCHED}/src/usb_tx.pio)
    pico_generate_pio_header(rkmoon_pio_ll ${PIO_PATCHED}/src/usb_rx.pio)
    target_include_directories(rkmoon_pio_ll PUBLIC ${PIO_PATCHED}/src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(rkmoon_pio_ll PUBLIC pico_stdlib hardware_pio hardware_dma)
    target_sources(rkmoon_rp2350_hid PRIVATE src/pio_cdc.c src/core/cdc_control.c)
    target_compile_definitions(rkmoon_rp2350_hid PRIVATE RKMOON_CDC_PID=${RKMOON_CDC_PID})
    target_link_libraries(rkmoon_rp2350_hid PRIVATE rkmoon_pio_ll pico_multicore)
endif()
