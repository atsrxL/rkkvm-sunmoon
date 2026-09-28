// SPDX-License-Identifier: GPL-3.0-or-later
// Hardware-independent core of the RKMoon RP2350 HID bridge.
//
// The core consumes protocol v1 bytes from BRIDGE_NUM_CH control channels,
// keeps the logical keyboard/mouse state, owns one HID report queue per
// interface (relative-motion splitting and merging) and runs the 500 ms
// release watchdog. The firmware shell only moves bytes and USB reports.
#ifndef RKMOON_BRIDGE_H
#define RKMOON_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRIDGE_FW_MAJOR 0u
#define BRIDGE_FW_MINOR 2u

#define BRIDGE_NUM_CH 2u
#define BRIDGE_CH_UART 0u
#define BRIDGE_CH_PIO_CDC 1u
#define BRIDGE_CH_NONE 0xFFu

#define BRIDGE_WATCHDOG_MS 500u
#define BRIDGE_RX_TIMEOUT_MS 50u
#define BRIDGE_WAKEUP_RETRY_MS 100u
#define BRIDGE_LED_INPUT_MS 60u
#define BRIDGE_LED_WATCHDOG_MS 2000u

#define BRIDGE_KB_QLEN 32u
#define BRIDGE_REL_QLEN 16u
#define BRIDGE_ABS_QLEN 16u

#define BRIDGE_KB_REPORT_LEN 8u  // modifiers, reserved, keys[6] (boot layout)
#define BRIDGE_REL_REPORT_LEN 7u // buttons, dx i16, dy i16, wheel i8, pan i8
#define BRIDGE_REL_BOOT_REPORT_LEN 3u // boot protocol: buttons, dx i8, dy i8
#define BRIDGE_ABS_REPORT_LEN 7u // buttons, x u16, y u16, wheel i8, pan i8
#define BRIDGE_MAX_REPORT_LEN 8u

#define BRIDGE_REL_XY_MAX 32767
#define BRIDGE_REL_BOOT_XY_MAX 127
#define BRIDGE_WHEEL_MAX 127
#define BRIDGE_ABS_MAX 32767u
#define BRIDGE_BUTTON_MASK 0x1Fu

#ifndef RKMOON_PIO_USB_CDC
#define RKMOON_PIO_USB_CDC 1
#endif
#define BRIDGE_CAPS (0x0Fu | (RKMOON_PIO_USB_CDC ? 0x10u : 0u)) // bit4: channel B

#define BRIDGE_ST_CONFIGURED 0x01u
#define BRIDGE_ST_SUSPENDED 0x02u
#define BRIDGE_ST_PRESSED 0x04u
#define BRIDGE_ST_WATCHDOG 0x08u

typedef enum {
    BRIDGE_IF_KEYBOARD = 0,
    BRIDGE_IF_MOUSE_REL = 1,
    BRIDGE_IF_MOUSE_ABS = 2,
    BRIDGE_IF_COUNT = 3,
} bridge_iface_t;

typedef enum {
    BRIDGE_LED_RED = 0,    // USB not configured
    BRIDGE_LED_GREEN = 1,  // USB configured, idle
    BRIDGE_LED_BLUE = 2,   // input accepted within BRIDGE_LED_INPUT_MS
    BRIDGE_LED_YELLOW = 3, // release watchdog fired within BRIDGE_LED_WATCHDOG_MS
} bridge_led_t;

typedef struct {
    void *ctx;
    // Send one encoded reply frame on a control channel.
    void (*send)(void *ctx, uint8_t channel, const uint8_t *frame, size_t len);
    // Ask the USB stack to signal remote wakeup (only called while suspended).
    void (*remote_wakeup)(void *ctx);
} bridge_io_t;

typedef struct {
    uint8_t r[BRIDGE_KB_REPORT_LEN];
} bridge_kb_entry_t;

typedef struct {
    uint8_t buttons;
    int32_t dx, dy, wheel, pan; // remaining (not yet reported) motion
} bridge_rel_entry_t;

typedef struct {
    uint8_t buttons;
    uint16_t x, y;
    int32_t wheel, pan;
} bridge_abs_entry_t;

typedef struct {
    bridge_io_t io;
    uint8_t serial[8];

    proto_parser_t parser[BRIDGE_NUM_CH];
    uint32_t last_byte_ms[BRIDGE_NUM_CH];
    uint8_t active_ch; // channel of the last accepted HELLO
    uint8_t cur_ch;    // channel of the byte being parsed
    uint32_t cur_ms;   // time of the byte being parsed

    bool mounted;
    bool suspended;
    bool rel_boot; // host selected HID boot protocol on the relative mouse

    bridge_kb_entry_t kb_q[BRIDGE_KB_QLEN];
    uint8_t kb_head, kb_count;
    bridge_rel_entry_t rel_q[BRIDGE_REL_QLEN];
    uint8_t rel_head, rel_count;
    bridge_abs_entry_t abs_q[BRIDGE_ABS_QLEN];
    uint8_t abs_head, abs_count;

    // Logical state as last commanded by the active host.
    uint8_t kb_state[BRIDGE_KB_REPORT_LEN];
    uint8_t rel_buttons;
    uint8_t abs_buttons;
    uint16_t abs_x, abs_y;
    bool abs_valid;

    // State handed to the USB stack (committed reports).
    uint8_t kb_sent[BRIDGE_KB_REPORT_LEN];
    uint8_t rel_sent_buttons;
    uint8_t abs_sent_buttons;
    uint16_t abs_sent_x, abs_sent_y;
    bool abs_sent_valid;

    uint32_t last_valid_ms;
    bool wd_fired;     // PONG bit3; sticky until the next accepted HELLO
    uint32_t wd_count; // watchdog releases since boot
    uint32_t wd_event_ms;
    bool input_seen;
    uint32_t last_input_ms;
    bool wakeup_pending;
    uint32_t last_wakeup_ms;

    uint32_t rx_errors;
    uint32_t dropped;
    uint32_t merged_rel; // statistics
    uint32_t wakeups;    // remote wakeup requests issued
} bridge_t;

void bridge_init(bridge_t *b, const bridge_io_t *io, const uint8_t serial[8]);

// USB stack state. A mount change forgets everything the host was sent.
// Entering suspend releases everything (the release is sent after resume).
void bridge_channel_lost(bridge_t *b, uint8_t channel);

void bridge_set_usb_state(bridge_t *b, bool mounted, bool suspended);

// Relative mouse protocol chosen by the host (SET_PROTOCOL). Boot protocol
// uses 3-byte reports with int8 motion; larger motion is split, not clamped.
void bridge_set_rel_boot(bridge_t *b, bool boot);

// True if the next byte may be parsed. One frame never needs more than one
// free slot per queue, so the shell stops feeding bytes (backpressure into its
// RX buffer) instead of dropping key or button changes.
bool bridge_can_accept(const bridge_t *b);

void bridge_rx_byte(bridge_t *b, uint8_t channel, uint8_t byte, uint32_t now_ms);
void bridge_note_rx_errors(bridge_t *b, uint32_t n); // e.g. UART framing errors
void bridge_note_dropped(bridge_t *b, uint32_t n);   // RX bytes lost to overflow

// Parser timeout and the 500 ms release watchdog.
void bridge_tick(bridge_t *b, uint32_t now_ms);

// Build the next report for iface without consuming it.
bool bridge_peek_report(const bridge_t *b, bridge_iface_t iface, uint8_t *buf, uint8_t *len);
// Mark the report returned by the last peek on iface as handed to USB.
void bridge_commit_report(bridge_t *b, bridge_iface_t iface);

void bridge_release_all(bridge_t *b);
bool bridge_any_pressed(const bridge_t *b);
uint8_t bridge_status(const bridge_t *b);
bridge_led_t bridge_led(const bridge_t *b, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
