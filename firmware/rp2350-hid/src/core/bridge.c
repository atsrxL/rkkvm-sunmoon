// SPDX-License-Identifier: GPL-3.0-or-later
#include "bridge.h"

#include <string.h>

#define ACC_LIMIT 0x3FFFFFFF

static int32_t sat_add(int32_t a, int32_t b) {
    int64_t s = (int64_t)a + (int64_t)b;
    if (s > ACC_LIMIT) {
        return ACC_LIMIT;
    }
    if (s < -ACC_LIMIT) {
        return -ACC_LIMIT;
    }
    return (int32_t)s;
}

static int32_t clampv(int32_t v, int32_t lim) {
    return v > lim ? lim : (v < -lim ? -lim : v);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static bool all_zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i]) {
            return false;
        }
    }
    return true;
}

static void clear_queues(bridge_t *b) {
    b->kb_head = b->kb_count = 0;
    b->rel_head = b->rel_count = 0;
    b->abs_head = b->abs_count = 0;
}

static void forget_host_state(bridge_t *b) {
    clear_queues(b);
    memset(b->kb_state, 0, sizeof(b->kb_state));
    b->rel_buttons = 0;
    b->abs_buttons = 0;
    b->abs_valid = false;
    memset(b->kb_sent, 0, sizeof(b->kb_sent));
    b->rel_sent_buttons = 0;
    b->abs_sent_buttons = 0;
    b->abs_sent_valid = false;
}

void bridge_init(bridge_t *b, const bridge_io_t *io, const uint8_t serial[8]) {
    memset(b, 0, sizeof(*b));
    b->io = *io;
    memcpy(b->serial, serial, 8);
    b->active_ch = BRIDGE_CH_NONE;
    for (unsigned i = 0; i < BRIDGE_NUM_CH; i++) {
        proto_parser_reset(&b->parser[i]);
    }
}

void bridge_set_usb_state(bridge_t *b, bool mounted, bool suspended) {
    if (mounted != b->mounted) {
        forget_host_state(b);
    }
    bool entering_suspend = mounted && b->mounted && suspended && !b->suspended;
    b->mounted = mounted;
    if (entering_suspend) {
        bridge_release_all(b);
    }
    b->suspended = suspended; // bus suspend is reported even before configuration
    if (!b->suspended) {
        b->wakeup_pending = false;
    }
}

void bridge_set_rel_boot(bridge_t *b, bool boot) {
    b->rel_boot = boot;
}

static int32_t rel_xy_limit(const bridge_t *b) {
    return b->rel_boot ? BRIDGE_REL_BOOT_XY_MAX : BRIDGE_REL_XY_MAX;
}

bool bridge_can_accept(const bridge_t *b) {
    return b->kb_count < BRIDGE_KB_QLEN && b->rel_count < BRIDGE_REL_QLEN && b->abs_count < BRIDGE_ABS_QLEN;
}

// ---- queues -------------------------------------------------------------

static void kb_push(bridge_t *b, const uint8_t r[BRIDGE_KB_REPORT_LEN]) {
    if (b->kb_count >= BRIDGE_KB_QLEN) {
        return; // unreachable while callers respect bridge_can_accept()
    }
    uint8_t idx = (uint8_t)((b->kb_head + b->kb_count) % BRIDGE_KB_QLEN);
    memcpy(b->kb_q[idx].r, r, BRIDGE_KB_REPORT_LEN);
    b->kb_count++;
}

// Merge only when the queue is nearly full and only into a tail entry that
// has the same buttons and is not the entry currently being reported. The
// last free slot is therefore always available for a button change.
static void rel_push(bridge_t *b, uint8_t buttons, int32_t dx, int32_t dy, int32_t wheel, int32_t pan) {
    if (b->rel_count >= 2 && b->rel_count >= BRIDGE_REL_QLEN - 1) {
        uint8_t tail = (uint8_t)((b->rel_head + b->rel_count - 1) % BRIDGE_REL_QLEN);
        bridge_rel_entry_t *t = &b->rel_q[tail];
        if (t->buttons == buttons) {
            t->dx = sat_add(t->dx, dx);
            t->dy = sat_add(t->dy, dy);
            t->wheel = sat_add(t->wheel, wheel);
            t->pan = sat_add(t->pan, pan);
            b->merged_rel++;
            return;
        }
    }
    if (b->rel_count >= BRIDGE_REL_QLEN) {
        return; // unreachable, see bridge_can_accept()
    }
    uint8_t idx = (uint8_t)((b->rel_head + b->rel_count) % BRIDGE_REL_QLEN);
    bridge_rel_entry_t *e = &b->rel_q[idx];
    e->buttons = buttons;
    e->dx = dx;
    e->dy = dy;
    e->wheel = wheel;
    e->pan = pan;
    b->rel_count++;
}

static void abs_push(bridge_t *b, uint8_t buttons, uint16_t x, uint16_t y, int32_t wheel, int32_t pan) {
    if (b->abs_count >= 2 && b->abs_count >= BRIDGE_ABS_QLEN - 1) {
        uint8_t tail = (uint8_t)((b->abs_head + b->abs_count - 1) % BRIDGE_ABS_QLEN);
        bridge_abs_entry_t *t = &b->abs_q[tail];
        if (t->buttons == buttons) {
            t->x = x;
            t->y = y;
            t->wheel = sat_add(t->wheel, wheel);
            t->pan = sat_add(t->pan, pan);
            return;
        }
    }
    if (b->abs_count >= BRIDGE_ABS_QLEN) {
        return; // unreachable, see bridge_can_accept()
    }
    uint8_t idx = (uint8_t)((b->abs_head + b->abs_count) % BRIDGE_ABS_QLEN);
    bridge_abs_entry_t *e = &b->abs_q[idx];
    e->buttons = buttons;
    e->x = x;
    e->y = y;
    e->wheel = wheel;
    e->pan = pan;
    b->abs_count++;
}

bool bridge_peek_report(const bridge_t *b, bridge_iface_t iface, uint8_t *buf, uint8_t *len) {
    switch (iface) {
    case BRIDGE_IF_KEYBOARD:
        if (!b->kb_count) {
            return false;
        }
        memcpy(buf, b->kb_q[b->kb_head].r, BRIDGE_KB_REPORT_LEN);
        *len = BRIDGE_KB_REPORT_LEN;
        return true;
    case BRIDGE_IF_MOUSE_REL: {
        if (!b->rel_count) {
            return false;
        }
        const bridge_rel_entry_t *e = &b->rel_q[b->rel_head];
        int32_t lim = rel_xy_limit(b);
        if (b->rel_boot) {
            buf[0] = (uint8_t)(e->buttons & 0x07u);
            buf[1] = (uint8_t)(int8_t)clampv(e->dx, lim);
            buf[2] = (uint8_t)(int8_t)clampv(e->dy, lim);
            *len = BRIDGE_REL_BOOT_REPORT_LEN;
            return true;
        }
        buf[0] = e->buttons;
        wr16(&buf[1], (uint16_t)(int16_t)clampv(e->dx, lim));
        wr16(&buf[3], (uint16_t)(int16_t)clampv(e->dy, lim));
        buf[5] = (uint8_t)(int8_t)clampv(e->wheel, BRIDGE_WHEEL_MAX);
        buf[6] = (uint8_t)(int8_t)clampv(e->pan, BRIDGE_WHEEL_MAX);
        *len = BRIDGE_REL_REPORT_LEN;
        return true;
    }
    case BRIDGE_IF_MOUSE_ABS: {
        if (!b->abs_count) {
            return false;
        }
        const bridge_abs_entry_t *e = &b->abs_q[b->abs_head];
        buf[0] = e->buttons;
        wr16(&buf[1], e->x);
        wr16(&buf[3], e->y);
        buf[5] = (uint8_t)(int8_t)clampv(e->wheel, BRIDGE_WHEEL_MAX);
        buf[6] = (uint8_t)(int8_t)clampv(e->pan, BRIDGE_WHEEL_MAX);
        *len = BRIDGE_ABS_REPORT_LEN;
        return true;
    }
    default:
        return false;
    }
}

void bridge_commit_report(bridge_t *b, bridge_iface_t iface) {
    switch (iface) {
    case BRIDGE_IF_KEYBOARD:
        if (!b->kb_count) {
            return;
        }
        memcpy(b->kb_sent, b->kb_q[b->kb_head].r, BRIDGE_KB_REPORT_LEN);
        b->kb_head = (uint8_t)((b->kb_head + 1) % BRIDGE_KB_QLEN);
        b->kb_count--;
        return;
    case BRIDGE_IF_MOUSE_REL: {
        if (!b->rel_count) {
            return;
        }
        bridge_rel_entry_t *e = &b->rel_q[b->rel_head];
        int32_t lim = rel_xy_limit(b);
        e->dx -= clampv(e->dx, lim);
        e->dy -= clampv(e->dy, lim);
        if (b->rel_boot) {
            // Boot reports carry no wheel/pan; drop them rather than stall.
            e->wheel = 0;
            e->pan = 0;
        } else {
            e->wheel -= clampv(e->wheel, BRIDGE_WHEEL_MAX);
            e->pan -= clampv(e->pan, BRIDGE_WHEEL_MAX);
        }
        b->rel_sent_buttons = e->buttons;
        if (!e->dx && !e->dy && !e->wheel && !e->pan) {
            b->rel_head = (uint8_t)((b->rel_head + 1) % BRIDGE_REL_QLEN);
            b->rel_count--;
        }
        return;
    }
    case BRIDGE_IF_MOUSE_ABS: {
        if (!b->abs_count) {
            return;
        }
        bridge_abs_entry_t *e = &b->abs_q[b->abs_head];
        e->wheel -= clampv(e->wheel, BRIDGE_WHEEL_MAX);
        e->pan -= clampv(e->pan, BRIDGE_WHEEL_MAX);
        b->abs_sent_buttons = e->buttons;
        b->abs_sent_x = e->x;
        b->abs_sent_y = e->y;
        b->abs_sent_valid = true;
        if (!e->wheel && !e->pan) {
            b->abs_head = (uint8_t)((b->abs_head + 1) % BRIDGE_ABS_QLEN);
            b->abs_count--;
        }
        return;
    }
    default:
        return;
    }
}

// ---- state --------------------------------------------------------------

static bool logical_pressed(const bridge_t *b) {
    return !all_zero(b->kb_state, sizeof(b->kb_state)) || b->rel_buttons || b->abs_buttons;
}

bool bridge_any_pressed(const bridge_t *b) {
    return logical_pressed(b) || !all_zero(b->kb_sent, sizeof(b->kb_sent)) || b->rel_sent_buttons ||
           b->abs_sent_buttons;
}

void bridge_release_all(bridge_t *b) {
    // Unsent reports are discarded: the release is what the host must see.
    clear_queues(b);
    memset(b->kb_state, 0, sizeof(b->kb_state));
    b->rel_buttons = 0;
    b->abs_buttons = 0;
    if (b->abs_sent_valid) {
        b->abs_x = b->abs_sent_x;
        b->abs_y = b->abs_sent_y;
    } else {
        b->abs_valid = false;
    }
    if (!b->mounted) {
        return;
    }
    if (!all_zero(b->kb_sent, sizeof(b->kb_sent))) {
        static const uint8_t zero[BRIDGE_KB_REPORT_LEN] = {0};
        kb_push(b, zero);
    }
    if (b->rel_sent_buttons) {
        rel_push(b, 0, 0, 0, 0, 0);
    }
    if (b->abs_sent_buttons) {
        abs_push(b, 0, b->abs_sent_x, b->abs_sent_y, 0, 0);
    }
}

uint8_t bridge_status(const bridge_t *b) {
    uint8_t s = 0;
    if (b->mounted) {
        s |= BRIDGE_ST_CONFIGURED;
    }
    if (b->suspended) {
        s |= BRIDGE_ST_SUSPENDED;
    }
    if (bridge_any_pressed(b)) {
        s |= BRIDGE_ST_PRESSED;
    }
    if (b->wd_fired) {
        s |= BRIDGE_ST_WATCHDOG;
    }
    return s;
}

bridge_led_t bridge_led(const bridge_t *b, uint32_t now_ms) {
    if (b->wd_count && (uint32_t)(now_ms - b->wd_event_ms) < BRIDGE_LED_WATCHDOG_MS) {
        return BRIDGE_LED_YELLOW;
    }
    if (b->input_seen && (uint32_t)(now_ms - b->last_input_ms) < BRIDGE_LED_INPUT_MS) {
        return BRIDGE_LED_BLUE;
    }
    return b->mounted ? BRIDGE_LED_GREEN : BRIDGE_LED_RED;
}

// ---- replies ------------------------------------------------------------

static void reply(bridge_t *b, uint8_t ch, uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t len) {
    uint8_t out[PROTO_MAX_FRAME];
    size_t n = proto_encode(out, type, seq, payload, len);
    if (n && b->io.send) {
        b->io.send(b->io.ctx, ch, out, n);
    }
}

static void nak(bridge_t *b, uint8_t ch, uint8_t type, uint8_t seq, uint8_t reason) {
    uint8_t p[2] = {type, reason};
    reply(b, ch, PROTO_T_NAK, seq, p, sizeof(p));
}

static uint16_t sat16(uint32_t v) {
    return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v;
}

static void send_pong(bridge_t *b, uint8_t ch, uint8_t seq) {
    uint8_t p[5];
    p[0] = bridge_status(b);
    wr16(&p[1], sat16(b->rx_errors));
    wr16(&p[3], sat16(b->dropped));
    reply(b, ch, PROTO_T_PONG, seq, p, sizeof(p));
}

static void send_info(bridge_t *b, uint8_t ch, uint8_t seq) {
    uint8_t p[12] = {PROTO_VERSION, BRIDGE_FW_MAJOR, BRIDGE_FW_MINOR, BRIDGE_CAPS};
    memcpy(&p[4], b->serial, 8);
    reply(b, ch, PROTO_T_INFO, seq, p, sizeof(p));
}

// ---- frame handling -----------------------------------------------------

static void mark_input(bridge_t *b) {
    b->input_seen = true;
    b->last_input_ms = b->cur_ms;
}

static void apply_keyboard(bridge_t *b, const uint8_t *p) {
    uint8_t r[BRIDGE_KB_REPORT_LEN] = {p[0], 0, p[1], p[2], p[3], p[4], p[5], p[6]};
    mark_input(b);
    if (memcmp(r, b->kb_state, sizeof(r)) == 0) {
        return;
    }
    memcpy(b->kb_state, r, sizeof(r));
    kb_push(b, r);
}

static void apply_mouse_rel(bridge_t *b, const uint8_t *p) {
    uint8_t buttons = (uint8_t)(p[0] & BRIDGE_BUTTON_MASK);
    int32_t dx = (int16_t)rd16(&p[1]);
    int32_t dy = (int16_t)rd16(&p[3]);
    int32_t wheel = (int8_t)p[5];
    int32_t pan = (int8_t)p[6];
    mark_input(b);
    if (buttons == b->rel_buttons && !dx && !dy && !wheel && !pan) {
        return;
    }
    b->rel_buttons = buttons;
    rel_push(b, buttons, dx, dy, wheel, pan);
}

static void apply_mouse_abs(bridge_t *b, const uint8_t *p) {
    uint8_t buttons = (uint8_t)(p[0] & BRIDGE_BUTTON_MASK);
    uint16_t x = rd16(&p[1]);
    uint16_t y = rd16(&p[3]);
    int32_t wheel = (int8_t)p[5];
    int32_t pan = (int8_t)p[6];
    if (x > BRIDGE_ABS_MAX) {
        x = BRIDGE_ABS_MAX;
    }
    if (y > BRIDGE_ABS_MAX) {
        y = BRIDGE_ABS_MAX;
    }
    mark_input(b);
    if (b->abs_valid && buttons == b->abs_buttons && x == b->abs_x && y == b->abs_y && !wheel && !pan) {
        return;
    }
    b->abs_valid = true;
    b->abs_buttons = buttons;
    b->abs_x = x;
    b->abs_y = y;
    abs_push(b, buttons, x, y, wheel, pan);
}

static bool frame_presses(const proto_frame_t *f) {
    if (f->type == PROTO_T_KEYBOARD) {
        return !all_zero(f->payload, 7);
    }
    return (f->payload[0] & BRIDGE_BUTTON_MASK) != 0;
}

static void request_wakeup(bridge_t *b) {
    if (b->wakeup_pending && (uint32_t)(b->cur_ms - b->last_wakeup_ms) < BRIDGE_WAKEUP_RETRY_MS) {
        return;
    }
    b->wakeup_pending = true;
    b->last_wakeup_ms = b->cur_ms;
    b->wakeups++;
    if (b->io.remote_wakeup) {
        b->io.remote_wakeup(b->io.ctx);
    }
}

static void handle_frame(bridge_t *b, const proto_frame_t *f) {
    uint8_t ch = b->cur_ch;
    switch (f->type) {
    case PROTO_T_HELLO:
        if (f->len != 1) {
            nak(b, ch, f->type, f->seq, PROTO_NAK_LENGTH);
            return;
        }
        if (f->payload[0] != PROTO_VERSION) {
            nak(b, ch, f->type, f->seq, PROTO_NAK_BAD_VERSION);
            return;
        }
        b->active_ch = ch;
        b->wd_fired = false;
        b->last_valid_ms = b->cur_ms;
        bridge_release_all(b);
        send_info(b, ch, f->seq);
        return;
    case PROTO_T_PING:
        if (f->len != 0) {
            nak(b, ch, f->type, f->seq, PROTO_NAK_LENGTH);
            return;
        }
        if (ch == b->active_ch) {
            b->last_valid_ms = b->cur_ms;
        }
        send_pong(b, ch, f->seq);
        return;
    case PROTO_T_KEYBOARD:
    case PROTO_T_MOUSE_REL:
    case PROTO_T_MOUSE_ABS:
    case PROTO_T_RELEASE_ALL: {
        uint8_t want = f->type == PROTO_T_RELEASE_ALL ? 0 : 7;
        if (f->len != want) {
            nak(b, ch, f->type, f->seq, PROTO_NAK_LENGTH);
            return;
        }
        if (ch != b->active_ch) {
            nak(b, ch, f->type, f->seq, PROTO_NAK_NO_SESSION);
            return;
        }
        b->last_valid_ms = b->cur_ms;
        if (f->type == PROTO_T_RELEASE_ALL) {
            bridge_release_all(b);
            return;
        }
        if (!b->mounted) {
            nak(b, ch, f->type, f->seq, PROTO_NAK_USB_NOT_READY);
            return;
        }
        if (b->suspended) {
            if (frame_presses(f)) {
                request_wakeup(b);
            }
            nak(b, ch, f->type, f->seq, PROTO_NAK_USB_NOT_READY);
            return;
        }
        if (f->type == PROTO_T_KEYBOARD) {
            apply_keyboard(b, f->payload);
        } else if (f->type == PROTO_T_MOUSE_REL) {
            apply_mouse_rel(b, f->payload);
        } else {
            apply_mouse_abs(b, f->payload);
        }
        return;
    }
    default:
        nak(b, ch, f->type, f->seq, PROTO_NAK_UNKNOWN_TYPE);
        return;
    }
}

static void parser_cb(void *ctx, proto_event_t ev, const proto_frame_t *f) {
    bridge_t *b = (bridge_t *)ctx;
    switch (ev) {
    case PROTO_EV_FRAME:
        handle_frame(b, f);
        return;
    case PROTO_EV_ERR_CRC:
        bridge_note_rx_errors(b, 1);
        nak(b, b->cur_ch, f->type, f->seq, PROTO_NAK_CRC);
        return;
    case PROTO_EV_ERR_LENGTH:
        bridge_note_rx_errors(b, 1);
        nak(b, b->cur_ch, f->type, f->seq, PROTO_NAK_LENGTH);
        return;
    }
}

void bridge_rx_byte(bridge_t *b, uint8_t channel, uint8_t byte, uint32_t now_ms) {
    if (channel >= BRIDGE_NUM_CH) {
        return;
    }
    b->cur_ch = channel;
    b->cur_ms = now_ms;
    b->last_byte_ms[channel] = now_ms;
    proto_parser_feed(&b->parser[channel], byte, parser_cb, b);
}

void bridge_note_rx_errors(bridge_t *b, uint32_t n) {
    b->rx_errors = (b->rx_errors + n < b->rx_errors) ? UINT32_MAX : b->rx_errors + n;
}

void bridge_note_dropped(bridge_t *b, uint32_t n) {
    b->dropped = (b->dropped + n < b->dropped) ? UINT32_MAX : b->dropped + n;
}

void bridge_tick(bridge_t *b, uint32_t now_ms) {
    bool accepting = bridge_can_accept(b);
    for (unsigned ch = 0; ch < BRIDGE_NUM_CH; ch++) {
        if (!accepting) {
            // Bytes are waiting in the shell's RX buffer, not missing.
            b->last_byte_ms[ch] = now_ms;
            continue;
        }
        if (proto_parser_in_frame(&b->parser[ch]) &&
            (uint32_t)(now_ms - b->last_byte_ms[ch]) > BRIDGE_RX_TIMEOUT_MS) {
            proto_parser_reset(&b->parser[ch]);
            bridge_note_rx_errors(b, 1);
        }
    }
    if (logical_pressed(b) && (uint32_t)(now_ms - b->last_valid_ms) > BRIDGE_WATCHDOG_MS) {
        bridge_release_all(b);
        b->wd_fired = true;
        b->wd_count++;
        b->wd_event_ms = now_ms;
    }
}
