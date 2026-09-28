// SPDX-License-Identifier: GPL-3.0-or-later
// RKMoon RP2350 HID bridge firmware shell (docs/ADR-012-rp2350-hid-bridge.md).
//
// Native USB-C: composite HID device (TinyUSB). UART0 GP0/GP1 1 Mbaud:
// protocol v1 control channel A. All protocol, watchdog and report-queue
// logic lives in src/core (host unit-tested); this file only moves bytes.
#include <string.h>

#include "bridge.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "status_led.h"
#include "tusb.h"
#include "uart_link.h"
#include "usb_descriptors.h"

#if RKMOON_PIO_USB_CDC
#include "pio_cdc.h"
#endif

// Chip watchdog: if the main loop ever stalls, the chip resets, USB drops
// and the host releases every key. Much longer than any legitimate loop.
#define CHIP_WATCHDOG_MS 1000u
// Max UART bytes parsed per loop so USB and the watchdog keep running.
#define RX_BUDGET 256

static bridge_t g_bridge;

static void io_send(void *ctx, uint8_t channel, const uint8_t *frame, size_t len) {
    (void)ctx;
    if (channel == BRIDGE_CH_UART) {
        uart_link_write(frame, len);
#if RKMOON_PIO_USB_CDC
    } else if (channel == BRIDGE_CH_PIO_CDC) {
        if (!pio_cdc_write(frame, len)) bridge_note_dropped(&g_bridge, (uint32_t)len);
#endif
    }
}

static void io_remote_wakeup(void *ctx) {
    (void)ctx;
    tud_remote_wakeup(); // no-op unless suspended and enabled by the host
}

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void pump_uart(uint32_t t) {
    uint32_t line_errors, dropped;
    uart_link_take_errors(&line_errors, &dropped);
    if (line_errors) {
        bridge_note_rx_errors(&g_bridge, line_errors);
    }
    if (dropped) {
        bridge_note_dropped(&g_bridge, dropped);
    }
    uint8_t byte;
    for (int i = 0; i < RX_BUDGET && bridge_can_accept(&g_bridge) && uart_link_peek(&byte); i++) {
        uart_link_pop();
        bridge_rx_byte(&g_bridge, BRIDGE_CH_UART, byte, t);
    }
}

static void pump_usb_reports(void) {
    for (uint8_t itf = 0; itf < BRIDGE_IF_COUNT; itf++) {
        if (!tud_hid_n_ready(itf)) {
            continue;
        }
        uint8_t buf[BRIDGE_MAX_REPORT_LEN];
        uint8_t len = 0;
        if (!bridge_peek_report(&g_bridge, (bridge_iface_t)itf, buf, &len)) {
            continue;
        }
        if (tud_hid_n_report(itf, 0, buf, len)) {
            bridge_commit_report(&g_bridge, (bridge_iface_t)itf);
        }
    }
}

int main(void) {
#if RKMOON_PIO_USB_CDC
    set_sys_clock_khz(120000, true);
#endif
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    usb_descriptors_set_serial(id.id);

    bridge_io_t io = {.ctx = NULL, .send = io_send, .remote_wakeup = io_remote_wakeup};
    bridge_init(&g_bridge, &io, id.id);

    status_led_init();
    status_led_set(BRIDGE_LED_RED);
    uart_link_init();
    tud_init(0);
    watchdog_enable(CHIP_WATCHDOG_MS, true);
#if RKMOON_PIO_USB_CDC
    pio_cdc_init(id.id);
#endif

    for (;;) {
        watchdog_update();
        tud_task();
        uint32_t t = now_ms();
        bridge_set_usb_state(&g_bridge, tud_mounted(), tud_suspended());
        pump_uart(t);
#if RKMOON_PIO_USB_CDC
        pio_cdc_pump(&g_bridge, t);
#endif
        bridge_tick(&g_bridge, t);
        pump_usb_reports();
        uart_link_poll_tx();
        status_led_set(bridge_led(&g_bridge, t));
    }
}

// ---- TinyUSB callbacks ------------------------------------------------------

void tud_mount_cb(void) {
    bridge_set_rel_boot(&g_bridge, false); // TinyUSB resets to report protocol
}

void tud_umount_cb(void) {
    bridge_set_rel_boot(&g_bridge, false);
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    if (instance == ITF_NUM_MOUSE_REL) {
        bridge_set_rel_boot(&g_bridge, protocol == HID_PROTOCOL_BOOT);
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)report_id;
    (void)report_type;
    // GET_REPORT is rare (some BIOSes); answer with an idle report.
    uint16_t n = instance == ITF_NUM_KEYBOARD ? BRIDGE_KB_REPORT_LEN : BRIDGE_ABS_REPORT_LEN;
    if (n > reqlen) {
        n = reqlen;
    }
    memset(buffer, 0, n);
    return n;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t bufsize) {
    // Keyboard LED output reports (Caps/Num Lock) are accepted and ignored in v1.
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}
