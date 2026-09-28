// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the firmware core against the shared protocol vectors
// (docs/rp2350-protocol-vectors.json), which the Windows client also uses.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/core/bridge.h"
#include "../src/core/crc16.h"
#include "../src/core/proto.h"
#include "vectors.h"

int vectors_run(int *checks);

static int fails;
static int *nchecks;
#define VCHECK(cond, name)                                                         \
    do {                                                                           \
        (*nchecks)++;                                                              \
        if (!(cond)) {                                                             \
            fails++;                                                               \
            fprintf(stderr, "vector %s: CHECK failed: %s (line %d)\n", name, #cond, __LINE__); \
        }                                                                          \
    } while (0)

typedef struct {
    proto_frame_t got[16];
    int n;
    int errors;
} collect_t;

static void collect_cb(void *ctx, proto_event_t ev, const proto_frame_t *f) {
    collect_t *c = (collect_t *)ctx;
    if (ev == PROTO_EV_FRAME) {
        if (c->n < 16) {
            c->got[c->n++] = *f;
        }
    } else {
        c->errors++;
    }
}

// Reply capture for the bridge-level INFO/PONG/NAK vectors.
static uint8_t cap[64];
static size_t cap_len;
static void cap_send(void *ctx, uint8_t ch, const uint8_t *frame, size_t len) {
    (void)ctx;
    (void)ch;
    if (len <= sizeof(cap)) {
        memcpy(cap, frame, len);
        cap_len = len;
    }
}

static void feed_frame(bridge_t *b, uint8_t type, uint8_t seq, const uint8_t *p, uint8_t len) {
    uint8_t buf[PROTO_MAX_FRAME];
    size_t n = proto_encode(buf, type, seq, p, len);
    for (size_t i = 0; i < n; i++) {
        bridge_rx_byte(b, BRIDGE_CH_UART, buf[i], 0);
    }
}

int vectors_run(int *checks) {
    nchecks = checks;
    const uint8_t *ascii = (const uint8_t *)VEC_CRC_CHECK_ASCII;
    VCHECK(crc16_ccitt_false(ascii, strlen(VEC_CRC_CHECK_ASCII)) == VEC_CRC_CHECK, "crc");

    for (size_t i = 0; i < sizeof(VEC_FRAMES) / sizeof(VEC_FRAMES[0]); i++) {
        const vec_frame_t *v = &VEC_FRAMES[i];
        uint8_t buf[PROTO_MAX_FRAME];
        size_t n = proto_encode(buf, v->type, v->seq, v->payload, (uint8_t)v->plen);
        VCHECK(n == v->flen && memcmp(buf, v->frame, n) == 0, v->name);
        proto_parser_t p;
        proto_parser_reset(&p);
        collect_t c = {0};
        for (size_t k = 0; k < v->flen; k++) {
            proto_parser_feed(&p, v->frame[k], collect_cb, &c);
        }
        VCHECK(c.n == 1 && c.errors == 0, v->name);
        VCHECK(c.got[0].type == v->type && c.got[0].seq == v->seq && c.got[0].len == v->plen &&
                   memcmp(c.got[0].payload, v->payload, v->plen) == 0,
               v->name);
    }

    for (size_t i = 0; i < sizeof(VEC_STREAMS) / sizeof(VEC_STREAMS[0]); i++) {
        const vec_stream_t *s = &VEC_STREAMS[i];
        proto_parser_t p;
        proto_parser_reset(&p);
        collect_t c = {0};
        for (size_t k = 0; k < s->slen; k++) {
            proto_parser_feed(&p, s->stream[k], collect_cb, &c);
        }
        VCHECK(c.errors == s->rx_errors, s->name);
        VCHECK((size_t)c.n == s->nexpect, s->name);
        for (size_t k = 0; k < s->nexpect && k < (size_t)c.n; k++) {
            const vec_expect_t *e = &s->expect[k];
            VCHECK(c.got[k].type == e->type && c.got[k].seq == e->seq && c.got[k].len == e->plen &&
                       memcmp(c.got[k].payload, e->payload, e->plen) == 0,
                   s->name);
        }
    }

    // Bridge-generated replies must match the board_to_host vectors byte for byte.
    for (size_t i = 0; i < sizeof(VEC_FRAMES) / sizeof(VEC_FRAMES[0]); i++) {
        const vec_frame_t *v = &VEC_FRAMES[i];
        if (v->to_board) {
            continue;
        }
        bridge_t b;
        bridge_io_t io = {.ctx = NULL, .send = cap_send, .remote_wakeup = NULL};
        cap_len = 0;
        if (v->type == PROTO_T_INFO) {
            // All firmware versions remain wire/decode vectors; generate only
            // the INFO corresponding to this build's version and capabilities.
            if (v->payload[2] != BRIDGE_FW_MINOR || v->payload[3] != BRIDGE_CAPS) continue;
            bridge_init(&b, &io, &v->payload[4]);
            bridge_set_usb_state(&b, true, false);
            uint8_t ver = PROTO_VERSION;
            feed_frame(&b, PROTO_T_HELLO, v->seq, &ver, 1);
        } else if (v->type == PROTO_T_PONG) {
            static const uint8_t serial[8] = {0};
            bridge_init(&b, &io, serial);
            uint8_t st = v->payload[0];
            bridge_set_usb_state(&b, (st & BRIDGE_ST_CONFIGURED) != 0, (st & BRIDGE_ST_SUSPENDED) != 0);
            b.wd_fired = (st & BRIDGE_ST_WATCHDOG) != 0;
            b.rx_errors = (uint32_t)(v->payload[1] | (v->payload[2] << 8));
            b.dropped = (uint32_t)(v->payload[3] | (v->payload[4] << 8));
            if (b.dropped == 0xFFFF) {
                b.dropped = 1000000; // must saturate to 0xFFFF on the wire
            }
            feed_frame(&b, PROTO_T_PING, v->seq, NULL, 0);
        } else if (v->type == PROTO_T_NAK) {
            static const uint8_t serial[8] = {0};
            bridge_init(&b, &io, serial);
            uint8_t reason = v->payload[1];
            uint8_t orig = v->payload[0];
            uint8_t ver = PROTO_VERSION;
            bridge_set_usb_state(&b, reason != PROTO_NAK_USB_NOT_READY, false);
            feed_frame(&b, PROTO_T_HELLO, 0, &ver, 1);
            uint8_t zero7[7] = {0};
            feed_frame(&b, orig, v->seq, zero7, orig == PROTO_T_KEYBOARD ? 7 : 0);
        } else {
            continue;
        }
        VCHECK(cap_len == v->flen && memcmp(cap, v->frame, v->flen) == 0, v->name);
    }
    return fails;
}
