/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Board header for Waveshare RP2350-USB-C (SKU 34641). Not present in
 * pico-sdk 2.3.1; written from the official schematic
 * (github.com/waveshareteam/RP2350-USB-C hardware/schematics/RP2350-USB-C.pdf,
 * repo commit 774cbb1e8ecc9d5cc8474ba9feb90a0939047f3b) and cross-checked with
 * the Waveshare PIO-USB example (examples/C/01_USB/src/pio_usb_configuration.h:
 * RP2350_USB_C -> PIO_USB_DP_PIN_DEFAULT 12, PIO_USB_PINOUT_DPDM).
 *
 *   RP2350A, W25Q16JV 2 MB flash, 12 MHz crystal
 *   GPIO16 -> WS2812B DIN
 *   PIO-USB Type-C receptacle: GPIO12 -> 27R -> D+, GPIO13 -> 27R -> D-
 *     R13 1.5k D+ pull-up to 3V3 fitted (device/full-speed), R10 D- pull-up NC
 *     CC: R19 5.1k pull-down fitted (sink), VBUS -> VSYS
 *   No VBUS sense, no user LED on GPIO25.
 */

// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_WAVESHARE_RP2350_USB_C_H
#define _BOARDS_WAVESHARE_RP2350_USB_C_H

// For board detection
#define WAVESHARE_RP2350_USB_C

#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- WS2812 ---
#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 16
#endif

// --- PIO USB ---
#ifndef WAVESHARE_RP2350_USB_C_USB_DP_PIN
#define WAVESHARE_RP2350_USB_C_USB_DP_PIN 12
#endif
#ifndef WAVESHARE_RP2350_USB_C_USB_DM_PIN
#define WAVESHARE_RP2350_USB_C_USB_DM_PIN 13
#endif
#ifndef PICO_DEFAULT_PIO_USB_DP_PIN
#define PICO_DEFAULT_PIO_USB_DP_PIN WAVESHARE_RP2350_USB_C_USB_DP_PIN
#endif

// --- FLASH ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
