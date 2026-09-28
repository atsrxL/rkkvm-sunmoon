// SPDX-License-Identifier: GPL-3.0-or-later
// Host unit tests for the hardware-independent bridge core.
// Build: make -C firmware/rp2350-hid/tests
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/bridge.h"
#include "../src/core/crc16.h"
#include "../src/core/proto.h"

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        g_checks++;                                                                  \
        if (!(cond)) {                                                               \
            g_failures++;                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                            \
    } while (0)

// ---- fake io --------------------------------------------------------------

typedef struct {
    uint8_t ch;
    uint8_t type, seq, len;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} reply_t;

typedef struct {
    reply_t replies[64];
    int nreplies;
    int wakeups;
    proto_parser_t rp; // decodes replies
} fake_t;

static fake_t g_fake;

static void reply_cb(void *ctx, proto_event_t ev, const proto_frame_t *f) {
    (void)ctx;
    if (ev != PROTO_EV_FRAME || g_fake.nreplies >= 64) {
        fprintf(stderr, "bad reply frame ev=%d\n", (int)ev);
        g_failures++;
        return;
    }
    reply_t *r = &g_fake.replies[g_fake.nreplies++];
    r->type = f->type;
    r->seq = f->seq;
    r->len = f->len;
    memcpy(r->payload, f->payload, f->len);
}

static uint8_t g_send_ch;
static void fake_send(void *ctx, uint8_t ch, const uint8_t *frame, size_t len) {
    (void)ctx;
    g_send_ch = ch;
    int before = g_fake.nreplies;
    for (size_t i = 0; i < len; i++) {
        proto_parser_feed(&g_fake.rp, frame[i], reply_cb, NULL);
    }
    if (g_fake.nreplies == before + 1) {
        g_fake.replies[before].ch = ch;
    } else {
        g_failures++;
        fprintf(stderr, "reply was not exactly one frame\n");
    }
}

static void fake_wakeup(void *ctx) {
    (void)ctx;
    g_fake.wakeups++;
}

static const uint8_t k_serial[8] = {1, 2, 3, 4, 5, 6, 7, 8};

static void setup(bridge_t *b, bool mounted) {
    memset(&g_fake, 0, sizeof(g_fake));
    bridge_io_t io = {.ctx = NULL, .send = fake_send, .remote_wakeup = fake_wakeup};
    bridge_init(b, &io, k_serial);
    bridge_set_usb_state(b, mounted, false);
}

static uint8_t g_seq;

static void send_frame(bridge_t *b, uint8_t ch, uint8_t type, const uint8_t *p, uint8_t len, uint32_t now) {
    uint8_t buf[PROTO_MAX_FRAME];
    size_t n = proto_encode(buf, type, g_seq++, p, len);
    for (size_t i = 0; i < n; i++) {
        bridge_rx_byte(b, ch, buf[i], now);
    }
}

static void hello(bridge_t *b, uint8_t ch, uint32_t now) {
    uint8_t v = PROTO_VERSION;
    send_frame(b, ch, PROTO_T_HELLO, &v, 1, now);
}

static void kb(bridge_t *b, uint8_t mods, uint8_t key, uint32_t now) {
    uint8_t p[7] = {mods, key, 0, 0, 0, 0, 0};
    send_frame(b, BRIDGE_CH_UART, PROTO_T_KEYBOARD, p, 7, now);
}

static void rel(bridge_t *b, uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, int8_t pan, uint32_t now) {
    uint8_t p[7] = {buttons,
                    (uint8_t)((uint16_t)dx & 0xFF),
                    (uint8_t)((uint16_t)dx >> 8),
                    (uint8_t)((uint16_t)dy & 0xFF),
                    (uint8_t)((uint16_t)dy >> 8),
                    (uint8_t)wheel,
                    (uint8_t)pan};
    send_frame(b, BRIDGE_CH_UART, PROTO_T_MOUSE_REL, p, 7, now);
}

static void absm(bridge_t *b, uint8_t buttons, uint16_t x, uint16_t y, uint32_t now) {
    uint8_t p[7] = {buttons, (uint8_t)(x & 0xFF), (uint8_t)(x >> 8), (uint8_t)(y & 0xFF), (uint8_t)(y >> 8), 0, 0};
    send_frame(b, BRIDGE_CH_UART, PROTO_T_MOUSE_ABS, p, 7, now);
}

static bool pop(bridge_t *b, bridge_iface_t i, uint8_t *buf) {
    uint8_t len = 0;
    if (!bridge_peek_report(b, i, buf, &len)) {
        return false;
    }
    bridge_commit_report(b, i);
    return true;
}

static int16_t s16(const uint8_t *p) {
    return (int16_t)(uint16_t)(p[0] | (p[1] << 8));
}

int vectors_run(int *checks);

static const reply_t *last_reply(void) {
    return g_fake.nreplies ? &g_fake.replies[g_fake.nreplies - 1] : NULL;
}

// ---- tests ----------------------------------------------------------------

static void test_crc(void) {
    const uint8_t check[] = "123456789";
    CHECK(crc16_ccitt_false(check, 9) == 0x29B1);
    CHECK(crc16_ccitt_false(NULL, 0) == 0xFFFF);
    uint16_t c = crc16_ccitt_false_update(CRC16_CCITT_FALSE_INIT, check, 4);
    c = crc16_ccitt_false_update(c, check + 4, 5);
    CHECK(c == 0x29B1);
}

typedef struct {
    int frames, crc, length;
    proto_frame_t last;
} pcount_t;

static void pcount_cb(void *ctx, proto_event_t ev, const proto_frame_t *f) {
    pcount_t *c = (pcount_t *)ctx;
    if (ev == PROTO_EV_FRAME) {
        c->frames++;
        c->last = *f;
    } else if (ev == PROTO_EV_ERR_CRC) {
        c->crc++;
    } else {
        c->length++;
    }
}

static void feed(proto_parser_t *p, const uint8_t *d, size_t n, pcount_t *c) {
    for (size_t i = 0; i < n; i++) {
        proto_parser_feed(p, d[i], pcount_cb, c);
    }
}

static void test_parser(void) {
    proto_parser_t p;
    pcount_t c;
    uint8_t f[PROTO_MAX_FRAME];

    // Known vector: PING seq 7 -> A5 06 07 00 crc(06 07 00) LE.
    size_t n = proto_encode(f, PROTO_T_PING, 7, NULL, 0);
    CHECK(n == 6);
    const uint8_t hdr[3] = {0x06, 0x07, 0x00};
    uint16_t crc = crc16_ccitt_false(hdr, 3);
    CHECK(f[0] == 0xA5 && f[4] == (crc & 0xFF) && f[5] == (crc >> 8));

    // Garbage then frame.
    proto_parser_reset(&p);
    memset(&c, 0, sizeof(c));
    const uint8_t junk[] = {0x00, 0x11, 0xFF};
    feed(&p, junk, sizeof(junk), &c);
    feed(&p, f, n, &c);
    CHECK(c.frames == 1 && c.crc == 0 && c.last.type == PROTO_T_PING && c.last.seq == 7);

    // Max payload OK, len 33 rejected.
    uint8_t pl[32];
    for (int i = 0; i < 32; i++) {
        pl[i] = (uint8_t)i;
    }
    n = proto_encode(f, 0x55, 1, pl, 32);
    CHECK(n == 38);
    CHECK(proto_encode(f + 0, 0x55, 1, pl, 33) == 0);
    n = proto_encode(f, 0x55, 1, pl, 32);
    memset(&c, 0, sizeof(c));
    proto_parser_reset(&p);
    feed(&p, f, n, &c);
    CHECK(c.frames == 1 && c.last.len == 32 && memcmp(c.last.payload, pl, 32) == 0);

    memset(&c, 0, sizeof(c));
    proto_parser_reset(&p);
    const uint8_t badlen[] = {0xA5, 0x02, 0x00, 33};
    feed(&p, badlen, sizeof(badlen), &c);
    CHECK(c.length == 1 && !proto_parser_in_frame(&p));

    // Corrupted CRC then a good frame: one error, one frame.
    memset(&c, 0, sizeof(c));
    proto_parser_reset(&p);
    n = proto_encode(f, PROTO_T_PING, 1, NULL, 0);
    f[5] ^= 0x01;
    feed(&p, f, n, &c);
    n = proto_encode(f, PROTO_T_PING, 2, NULL, 0);
    feed(&p, f, n, &c);
    CHECK(c.crc == 1 && c.frames == 1 && c.last.seq == 2);

    // Resync: a real frame starts inside a truncated one (stray 0xA5 + len).
    memset(&c, 0, sizeof(c));
    proto_parser_reset(&p);
    uint8_t stream[64];
    size_t sn = 0;
    stream[sn++] = 0xA5; // fake sync
    stream[sn++] = 0x02; // fake type
    stream[sn++] = 0x00; // fake seq
    stream[sn++] = 0x07; // fake len -> needs 9 more bytes
    n = proto_encode(f, PROTO_T_PING, 9, NULL, 0);
    memcpy(&stream[sn], f, n);
    sn += n;
    n = proto_encode(f, PROTO_T_PING, 10, NULL, 0);
    memcpy(&stream[sn], f, n);
    sn += n;
    feed(&p, stream, sn, &c);
    CHECK(c.crc == 1);
    CHECK(c.frames == 2 && c.last.seq == 10);

    // Every single-bit flip of a frame is detected (never delivered as-is).
    n = proto_encode(f, PROTO_T_KEYBOARD, 3, pl, 7);
    int undetected = 0;
    for (size_t byte = 1; byte < n; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t g[PROTO_MAX_FRAME];
            memcpy(g, f, n);
            g[byte] ^= (uint8_t)(1u << bit);
            memset(&c, 0, sizeof(c));
            proto_parser_reset(&p);
            feed(&p, g, n, &c);
            if (c.frames && c.last.type == PROTO_T_KEYBOARD && c.last.seq == 3 &&
                memcmp(c.last.payload, pl, 7) == 0) {
                // the unmodified frame cannot appear; any delivered frame must differ
                undetected++;
            }
            if (c.frames) {
                undetected++;
            }
        }
    }
    CHECK(undetected == 0);

    // Random fuzz: parser never crashes and never reports len > 32.
    srand(12345);
    proto_parser_reset(&p);
    memset(&c, 0, sizeof(c));
    for (int i = 0; i < 200000; i++) {
        uint8_t byte = (uint8_t)(rand() & 0xFF);
        if ((rand() & 7) == 0) {
            byte = 0xA5;
        }
        proto_parser_feed(&p, byte, pcount_cb, &c);
        CHECK(p.pos <= PROTO_MAX_FRAME);
    }
    CHECK(c.last.len <= PROTO_MAX_PAYLOAD);
}

static void test_hello_ping(void) {
    bridge_t b;
    setup(&b, true);
    uint8_t v = PROTO_VERSION;
    g_seq = 40;
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_HELLO, &v, 1, 10);
    const reply_t *r = last_reply();
    CHECK(r && r->type == PROTO_T_INFO && r->seq == 40 && r->len == 12);
    CHECK(r && r->payload[0] == 1 && r->payload[3] == BRIDGE_CAPS && memcmp(&r->payload[4], k_serial, 8) == 0);

    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 20);
    r = last_reply();
    CHECK(r && r->type == PROTO_T_PONG && r->len == 5 && r->payload[0] == BRIDGE_ST_CONFIGURED);

    uint8_t v2 = 2;
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_HELLO, &v2, 1, 30);
    r = last_reply();
    CHECK(r && r->type == PROTO_T_NAK && r->payload[0] == PROTO_T_HELLO && r->payload[1] == PROTO_NAK_BAD_VERSION);

    send_frame(&b, BRIDGE_CH_UART, 0x42, NULL, 0, 30);
    r = last_reply();
    CHECK(r && r->type == PROTO_T_NAK && r->payload[0] == 0x42 && r->payload[1] == PROTO_NAK_UNKNOWN_TYPE);

    uint8_t shortp[3] = {0};
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_KEYBOARD, shortp, 3, 30);
    r = last_reply();
    CHECK(r && r->type == PROTO_T_NAK && r->payload[1] == PROTO_NAK_LENGTH);

    // Valid input frames get no reply.
    int before = g_fake.nreplies;
    kb(&b, 0, 0x04, 40);
    CHECK(g_fake.nreplies == before);

    // CRC error -> NAK reason 1 and rx_errors counted in PONG.
    uint8_t f[PROTO_MAX_FRAME];
    size_t n = proto_encode(f, PROTO_T_PING, 99, NULL, 0);
    f[n - 1] ^= 0xFF;
    for (size_t i = 0; i < n; i++) {
        bridge_rx_byte(&b, BRIDGE_CH_UART, f[i], 50);
    }
    r = last_reply();
    CHECK(r && r->type == PROTO_T_NAK && r->payload[1] == PROTO_NAK_CRC);
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 60);
    r = last_reply();
    CHECK(r && r->type == PROTO_T_PONG && r->payload[1] == 1 && r->payload[2] == 0);
    CHECK(r && (r->payload[0] & BRIDGE_ST_PRESSED));
}

static void test_session_rules(void) {
    bridge_t b;
    setup(&b, true);
    // Input before any HELLO is rejected.
    kb(&b, 0, 0x04, 1);
    const reply_t *r = last_reply();
    CHECK(r && r->type == PROTO_T_NAK && r->payload[1] == PROTO_NAK_NO_SESSION);
    uint8_t rep[8];
    CHECK(!pop(&b, BRIDGE_IF_KEYBOARD, rep));

    hello(&b, BRIDGE_CH_UART, 2);
    kb(&b, 0x02, 0x04, 3); // shift+a
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep) && rep[0] == 0x02 && rep[1] == 0 && rep[2] == 0x04);

    // Channel B takes over with HELLO: everything released immediately.
    hello(&b, BRIDGE_CH_PIO_CDC, 4);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep));
    static const uint8_t zero[8] = {0};
    CHECK(memcmp(rep, zero, 8) == 0);
    CHECK(!bridge_any_pressed(&b));

    // Old channel is now rejected; PING still answered on it.
    kb(&b, 0, 0x05, 5);
    r = last_reply();
    CHECK(r && r->ch == BRIDGE_CH_UART && r->type == PROTO_T_NAK && r->payload[1] == PROTO_NAK_NO_SESSION);
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 6);
    r = last_reply();
    CHECK(r && r->ch == BRIDGE_CH_UART && r->type == PROTO_T_PONG);

    // Not mounted -> NAK 4, nothing queued.
    setup(&b, false);
    hello(&b, BRIDGE_CH_UART, 1);
    kb(&b, 0, 0x04, 2);
    r = last_reply();
    CHECK(r && r->type == PROTO_T_NAK && r->payload[1] == PROTO_NAK_USB_NOT_READY);
    CHECK(!pop(&b, BRIDGE_IF_KEYBOARD, rep));
}

static void test_keyboard_dedupe_release(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    uint8_t rep[8];
    kb(&b, 0, 0x04, 1);
    kb(&b, 0, 0x04, 2); // identical, not re-queued
    CHECK(b.kb_count == 1);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep) && rep[2] == 0x04);
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_RELEASE_ALL, NULL, 0, 3);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep) && rep[0] == 0 && rep[2] == 0);
    CHECK(!bridge_any_pressed(&b));
    // Release with nothing pressed queues nothing.
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_RELEASE_ALL, NULL, 0, 4);
    CHECK(!pop(&b, BRIDGE_IF_KEYBOARD, rep));

    // Release discards unsent presses; host never saw them, so no report.
    kb(&b, 0, 0x06, 5);
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_RELEASE_ALL, NULL, 0, 6);
    CHECK(!pop(&b, BRIDGE_IF_KEYBOARD, rep));
    CHECK(!bridge_any_pressed(&b));
}

static void test_watchdog(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 1000);
    uint8_t rep[8];
    kb(&b, 0, 0x04, 1000);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep));
    rel(&b, 0x01, 0, 0, 0, 0, 1000);
    CHECK(pop(&b, BRIDGE_IF_MOUSE_REL, rep) && rep[0] == 1);
    absm(&b, 0x02, 100, 200, 1000);
    CHECK(pop(&b, BRIDGE_IF_MOUSE_ABS, rep) && rep[0] == 2);

    // PINGs keep the session alive while keys are held.
    for (uint32_t t = 1200; t <= 2000; t += 200) {
        send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, t);
        bridge_tick(&b, t + 1);
    }
    CHECK(b.wd_count == 0 && bridge_any_pressed(&b));

    bridge_tick(&b, 2500); // exactly 500 ms: not yet
    CHECK(b.wd_count == 0);
    bridge_tick(&b, 2501);
    CHECK(b.wd_count == 1 && b.wd_fired);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep) && rep[2] == 0);
    CHECK(pop(&b, BRIDGE_IF_MOUSE_REL, rep) && rep[0] == 0);
    CHECK(pop(&b, BRIDGE_IF_MOUSE_ABS, rep) && rep[0] == 0 && rep[1] == 100 && rep[3] == 200);
    CHECK(!bridge_any_pressed(&b));
    CHECK(bridge_led(&b, 2600) == BRIDGE_LED_YELLOW);
    CHECK(bridge_led(&b, 2501 + BRIDGE_LED_WATCHDOG_MS) == BRIDGE_LED_GREEN);

    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 2600);
    const reply_t *r = last_reply();
    CHECK(r && r->type == PROTO_T_PONG && (r->payload[0] & BRIDGE_ST_WATCHDOG) && !(r->payload[0] & BRIDGE_ST_PRESSED));
    bridge_tick(&b, 9000); // nothing pressed -> no further firing
    CHECK(b.wd_count == 1);
    hello(&b, BRIDGE_CH_UART, 9001); // HELLO clears the sticky bit
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 9002);
    r = last_reply();
    CHECK(r && !(r->payload[0] & BRIDGE_ST_WATCHDOG));

    // Idle (nothing pressed) never trips the watchdog.
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    rel(&b, 0, 10, 10, 0, 0, 0);
    bridge_tick(&b, 100000);
    CHECK(b.wd_count == 0);

    // Frames from a non-active channel do not feed the watchdog.
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    kb(&b, 0, 0x04, 0);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep));
    send_frame(&b, BRIDGE_CH_PIO_CDC, PROTO_T_PING, NULL, 0, 400);
    bridge_tick(&b, 501);
    CHECK(b.wd_count == 1);

    // Wrap-around of the millisecond clock.
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0xFFFFFF00u);
    kb(&b, 0, 0x04, 0xFFFFFF00u);
    bridge_tick(&b, 0x00000010u); // 272 ms later
    CHECK(b.wd_count == 0);
    bridge_tick(&b, 0x00000100u); // 512 ms later
    CHECK(b.wd_count == 1);
}

static void test_rel_split(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    uint8_t rep[8];
    rel(&b, 0, 32767, -32768, 0, 0, 1);
    CHECK(pop(&b, BRIDGE_IF_MOUSE_REL, rep));
    // -32768 is split into -32767 then -1 so both axes stay symmetric.
    CHECK(s16(&rep[1]) == 32767 && s16(&rep[3]) == -32767);
    CHECK(pop(&b, BRIDGE_IF_MOUSE_REL, rep));
    CHECK(s16(&rep[1]) == 0 && s16(&rep[3]) == -1);
    CHECK(!pop(&b, BRIDGE_IF_MOUSE_REL, rep));

    // Merged motion larger than one report is split without loss.
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    int64_t want_x = 0, want_y = 0, want_w = 0;
    for (int i = 0; i < 200; i++) {
        rel(&b, 0, 30000, -20000, 100, 0, 1);
        want_x += 30000;
        want_y -= 20000;
        want_w += 100;
    }
    CHECK(b.merged_rel > 0);
    int64_t got_x = 0, got_y = 0, got_w = 0;
    int reports = 0;
    while (pop(&b, BRIDGE_IF_MOUSE_REL, rep)) {
        int16_t x = s16(&rep[1]), y = s16(&rep[3]);
        CHECK(x != INT16_MIN && y != INT16_MIN); // symmetric range -32767..32767
        CHECK((int8_t)rep[5] >= -127 && (int8_t)rep[5] <= 127);
        got_x += x;
        got_y += y;
        got_w += (int8_t)rep[5];
        reports++;
    }
    CHECK(got_x == want_x && got_y == want_y && got_w == want_w);
    CHECK(reports >= (int)(want_x / 32767));
}

static void test_rel_merge_keeps_buttons(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    uint8_t rep[8];
    // Fill the queue with moves (host not polling), then press/release a
    // button: the button transitions must survive, motion must be summed.
    int total = 0;
    for (int i = 0; i < 100; i++) {
        rel(&b, 0, 1, 0, 0, 0, 1);
        total++;
        CHECK(bridge_can_accept(&b));
    }
    rel(&b, 1, 5, 0, 0, 0, 2); // press + move
    total += 5;
    for (int i = 0; i < 50; i++) {
        rel(&b, 1, 1, 0, 0, 0, 3);
        total++;
    }
    CHECK(!bridge_can_accept(&b) || b.rel_count < BRIDGE_REL_QLEN);
    // Emulate shell backpressure: drain one report whenever full.
    int drained_x = 0;
    uint8_t seen[64];
    int nseen = 0;
    while (!bridge_can_accept(&b)) {
        CHECK(pop(&b, BRIDGE_IF_MOUSE_REL, rep));
        drained_x += s16(&rep[1]);
        if (nseen < 64) {
            seen[nseen++] = rep[0];
        }
    }
    rel(&b, 0, 0, 0, 0, 0, 4); // release
    while (pop(&b, BRIDGE_IF_MOUSE_REL, rep)) {
        drained_x += s16(&rep[1]);
        if (nseen < 64) {
            seen[nseen++] = rep[0];
        }
    }
    CHECK(drained_x == total);
    // Button sequence must be 0...0,1...1,0 with exactly one press and one release.
    int transitions = 0;
    uint8_t prev = 0;
    for (int i = 0; i < nseen; i++) {
        if (seen[i] != prev) {
            transitions++;
        }
        prev = seen[i];
    }
    CHECK(transitions == 2 && prev == 0);
    CHECK(b.merged_rel > 0);
}

static void test_backpressure_keyboard(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    // 32 distinct changes fill the queue; the core then reports "full" and the
    // shell must stop feeding bytes; nothing is ever dropped.
    for (int i = 0; i < (int)BRIDGE_KB_QLEN; i++) {
        CHECK(bridge_can_accept(&b));
        kb(&b, 0, (uint8_t)(i & 1 ? 0 : 0x04), 1);
    }
    CHECK(!bridge_can_accept(&b));
    // While full, the watchdog timer continues but RX timeout does not fire.
    bridge_tick(&b, 400);
    CHECK(b.rx_errors == 0);
    uint8_t rep[8];
    int n = 0;
    while (pop(&b, BRIDGE_IF_KEYBOARD, rep)) {
        CHECK(rep[2] == (n & 1 ? 0 : 0x04));
        n++;
    }
    CHECK(n == (int)BRIDGE_KB_QLEN);
}

static void test_abs(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    uint8_t rep[8];
    absm(&b, 0, 40000, 32767, 1); // clamps x
    CHECK(pop(&b, BRIDGE_IF_MOUSE_ABS, rep));
    CHECK(rep[1] == 0xFF && rep[2] == 0x7F && rep[3] == 0xFF && rep[4] == 0x7F);
    absm(&b, 0, 32767, 32767, 2); // identical -> dedupe
    CHECK(!pop(&b, BRIDGE_IF_MOUSE_ABS, rep));
    // Saturating stream of moves coalesces to the latest position.
    for (int i = 0; i < 100; i++) {
        absm(&b, 0, (uint16_t)i, (uint16_t)(i * 2), 3);
    }
    uint8_t last[8];
    int n = 0;
    while (pop(&b, BRIDGE_IF_MOUSE_ABS, rep)) {
        memcpy(last, rep, 8);
        n++;
    }
    CHECK(n <= (int)BRIDGE_ABS_QLEN);
    CHECK(last[1] == 99 && last[3] == 198);
}

static void test_suspend_wakeup(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    bridge_set_usb_state(&b, true, true);
    rel(&b, 0, 5, 5, 0, 0, 10); // motion alone does not wake
    CHECK(g_fake.wakeups == 0);
    const reply_t *r = last_reply();
    CHECK(r && r->type == PROTO_T_NAK && r->payload[1] == PROTO_NAK_USB_NOT_READY);
    kb(&b, 0, 0x2C, 20);
    CHECK(g_fake.wakeups == 1);
    kb(&b, 0, 0x2C, 30); // rate limited
    CHECK(g_fake.wakeups == 1);
    kb(&b, 0, 0x2C, 20 + BRIDGE_WAKEUP_RETRY_MS);
    CHECK(g_fake.wakeups == 2);
    uint8_t rep[8];
    CHECK(!pop(&b, BRIDGE_IF_KEYBOARD, rep)); // nothing replayed after resume
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 200);
    r = last_reply();
    CHECK(r && (r->payload[0] & BRIDGE_ST_SUSPENDED) && !(r->payload[0] & BRIDGE_ST_PRESSED));
    bridge_set_usb_state(&b, true, false);
    kb(&b, 0, 0x2C, 300);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep) && rep[2] == 0x2C);

    // Unmount forgets the host state; nothing to release afterwards.
    bridge_set_usb_state(&b, false, false);
    CHECK(!bridge_any_pressed(&b));
    CHECK(bridge_led(&b, 10000) == BRIDGE_LED_RED);
}

static void test_rx_timeout_and_led(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    bridge_rx_byte(&b, BRIDGE_CH_UART, 0xA5, 100);
    bridge_rx_byte(&b, BRIDGE_CH_UART, 0x06, 100);
    bridge_tick(&b, 150);
    CHECK(b.rx_errors == 0);
    bridge_tick(&b, 151);
    CHECK(b.rx_errors == 1 && !proto_parser_in_frame(&b.parser[0]));
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 160);
    CHECK(last_reply()->type == PROTO_T_PONG);

    kb(&b, 0, 0x04, 1000);
    CHECK(bridge_led(&b, 1000) == BRIDGE_LED_BLUE);
    CHECK(bridge_led(&b, 1000 + BRIDGE_LED_INPUT_MS) == BRIDGE_LED_GREEN);

    bridge_note_dropped(&b, 70000);
    send_frame(&b, BRIDGE_CH_UART, PROTO_T_PING, NULL, 0, 1100);
    const reply_t *r = last_reply();
    CHECK(r->payload[3] == 0xFF && r->payload[4] == 0xFF); // saturates at 65535
}

static void test_boot_protocol(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    bridge_set_rel_boot(&b, true);
    uint8_t rep[8];
    uint8_t len = 0;
    rel(&b, 0x1F, 300, -200, 5, 0, 1);
    CHECK(bridge_peek_report(&b, BRIDGE_IF_MOUSE_REL, rep, &len) && len == BRIDGE_REL_BOOT_REPORT_LEN);
    CHECK(rep[0] == 0x07); // only 3 buttons in boot protocol
    int sx = 0, sy = 0, n = 0;
    while (pop(&b, BRIDGE_IF_MOUSE_REL, rep)) {
        CHECK((int8_t)rep[1] >= -127 && (int8_t)rep[2] >= -127);
        sx += (int8_t)rep[1];
        sy += (int8_t)rep[2];
        n++;
    }
    CHECK(sx == 300 && sy == -200 && n == 3);
    bridge_set_rel_boot(&b, false);
    rel(&b, 0, 300, 0, 0, 0, 2);
    CHECK(bridge_peek_report(&b, BRIDGE_IF_MOUSE_REL, rep, &len) && len == BRIDGE_REL_REPORT_LEN && s16(&rep[1]) == 300);
}

static void test_suspend_releases(void) {
    bridge_t b;
    setup(&b, true);
    hello(&b, BRIDGE_CH_UART, 0);
    uint8_t rep[8];
    kb(&b, 0x01, 0x04, 1);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep));
    bridge_set_usb_state(&b, true, true); // host suspends while a key is held
    
    bridge_tick(&b, 5000);
    CHECK(b.wd_count == 0); // nothing logically pressed any more
    bridge_set_usb_state(&b, true, false);
    CHECK(pop(&b, BRIDGE_IF_KEYBOARD, rep) && rep[0] == 0 && rep[2] == 0);
    CHECK(!bridge_any_pressed(&b));
}

int main(void) {
    test_crc();
    test_parser();
    test_hello_ping();
    test_session_rules();
    test_keyboard_dedupe_release();
    test_watchdog();
    test_rel_split();
    test_rel_merge_keeps_buttons();
    test_backpressure_keyboard();
    test_abs();
    test_suspend_wakeup();
    test_rx_timeout_and_led();
    test_boot_protocol();
    test_suspend_releases();
    int vf = vectors_run(&g_checks);
    g_failures += vf;
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
