// SPDX-License-Identifier: GPL-3.0-or-later
// RKMoon HID bridge serial protocol v1 (docs/ADR-012-rp2350-hid-bridge.md).
// Frame: 0xA5 | type | seq | len | payload[len] | crc16 LE (CRC over type..payload).
#ifndef RKMOON_PROTO_H
#define RKMOON_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_SYNC 0xA5u
#define PROTO_VERSION 1u
#define PROTO_MAX_PAYLOAD 32u
#define PROTO_HEADER_LEN 4u
#define PROTO_CRC_LEN 2u
#define PROTO_MAX_FRAME (PROTO_HEADER_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN)

enum {
    PROTO_T_HELLO = 0x01,
    PROTO_T_KEYBOARD = 0x02,
    PROTO_T_MOUSE_REL = 0x03,
    PROTO_T_MOUSE_ABS = 0x04,
    PROTO_T_RELEASE_ALL = 0x05,
    PROTO_T_PING = 0x06,
    PROTO_T_NAK = 0x80,
    PROTO_T_INFO = 0x81,
    PROTO_T_PONG = 0x86,
};

enum {
    PROTO_NAK_CRC = 1,
    PROTO_NAK_LENGTH = 2,
    PROTO_NAK_UNKNOWN_TYPE = 3,
    PROTO_NAK_USB_NOT_READY = 4,
    PROTO_NAK_BAD_VERSION = 5,
    PROTO_NAK_NO_SESSION = 6,
};

typedef struct {
    uint8_t type;
    uint8_t seq;
    uint8_t len;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} proto_frame_t;

typedef enum {
    PROTO_EV_FRAME = 0,      // CRC-valid frame (type/payload semantics not checked here)
    PROTO_EV_ERR_CRC = 1,    // frame->type/seq hold the unverified header
    PROTO_EV_ERR_LENGTH = 2, // len byte > PROTO_MAX_PAYLOAD
} proto_event_t;

typedef void (*proto_cb_t)(void *ctx, proto_event_t ev, const proto_frame_t *frame);

typedef struct {
    uint8_t buf[PROTO_MAX_FRAME];
    uint8_t pos; // bytes of the current candidate frame (0 = hunting for sync)
} proto_parser_t;

void proto_parser_reset(proto_parser_t *p);
bool proto_parser_in_frame(const proto_parser_t *p);

// Feed one byte. After a framing error, the bytes following the rejected 0xA5
// are re-scanned, so a real frame that started inside garbage is not lost.
void proto_parser_feed(proto_parser_t *p, uint8_t byte, proto_cb_t cb, void *ctx);

// Encode a frame into out (PROTO_MAX_FRAME bytes). Returns the frame size,
// or 0 if len > PROTO_MAX_PAYLOAD.
size_t proto_encode(uint8_t *out, uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif
