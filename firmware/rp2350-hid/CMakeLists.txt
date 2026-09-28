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

# Experimental control channel B (CDC ACM on the PIO-USB port). See README:
# TinyUSB in this SDK runs one device stack only, so this cannot coexist
# with the native HID device. Kept as an explicit switch, default OFF.
option(RKMOON_PIO_USB_CDC "Enable experimental PIO-USB CDC control channel" OFF)
if (RKMOON_PIO_USB_CDC)
    message(FATAL_ERROR
        "RKMOON_PIO_USB_CDC=ON is not supported: TinyUSB (pico-sdk 2.3.1, 0.18.0) has a single "
        "device stack (one usbd_device_t, HID class hard-codes rhport 0) and dcd_rp2040.c / "
        "dcd_pio_usb.c are mutually exclusive via CFG_TUD_RPI_PIO_USB. See firmware/rp2350-hid/README.md.")
endif ()

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
    RKMOON_PIO_USB_CDC=0
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
pico_set_program_version(rkmoon_rp2350_hid "0.1")
pico_add_extra_outputs(rkmoon_rp2350_hid)
