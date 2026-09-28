// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto.h"

#include <string.h>

#include "crc16.h"

void proto_parser_reset(proto_parser_t *p) {
    p->pos = 0;
}

bool proto_parser_in_frame(const proto_parser_t *p) {
    return p->pos != 0;
}

void proto_parser_feed(proto_parser_t *p, uint8_t byte, proto_cb_t cb, void *ctx) {
    // Pending bytes: the new byte plus bytes re-scanned after an error. The
    // content is always a suffix of the last PROTO_MAX_FRAME stream bytes.
    uint8_t q[PROTO_MAX_FRAME + 1];
    size_t qn = 0;
    size_t qi = 0;
    q[qn++] = byte;

    while (qi < qn) {
        uint8_t b = q[qi++];
        if (p->pos == 0) {
            if (b == PROTO_SYNC) {
                p->buf[p->pos++] = b;
            }
            continue;
        }
        p->buf[p->pos++] = b;
        if (p->pos < PROTO_HEADER_LEN) {
            continue;
        }

        proto_event_t err;
        if (p->buf[3] > PROTO_MAX_PAYLOAD) {
            err = PROTO_EV_ERR_LENGTH;
        } else {
            size_t total = PROTO_HEADER_LEN + (size_t)p->buf[3] + PROTO_CRC_LEN;
            if (p->pos < total) {
                continue;
            }
            uint16_t want = crc16_ccitt_false(&p->buf[1], (size_t)3 + p->buf[3]);
            uint16_t got = (uint16_t)(p->buf[total - 2] | ((uint16_t)p->buf[total - 1] << 8));
            if (want == got) {
                proto_frame_t f;
                f.type = p->buf[1];
                f.seq = p->buf[2];
                f.len = p->buf[3];
                memcpy(f.payload, &p->buf[PROTO_HEADER_LEN], f.len);
                p->pos = 0;
                cb(ctx, PROTO_EV_FRAME, &f);
                continue;
            }
            err = PROTO_EV_ERR_CRC;
        }

        proto_frame_t f;
        f.type = p->buf[1];
        f.seq = p->buf[2];
        f.len = 0;
        // Re-scan buf[1..pos) followed by anything still pending. Pending bytes
        // at this point are always empty or come after buf in stream order.
        uint8_t tmp[PROTO_MAX_FRAME + 1];
        size_t tn = 0;
        for (size_t i = 1; i < p->pos; i++) {
            tmp[tn++] = p->buf[i];
        }
        while (qi < qn) {
            tmp[tn++] = q[qi++];
        }
        memcpy(q, tmp, tn);
        qn = tn;
        qi = 0;
        p->pos = 0;
        cb(ctx, err, &f);
    }
}

size_t proto_encode(uint8_t *out, uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t len) {
    if (len > PROTO_MAX_PAYLOAD) {
        return 0;
    }
    out[0] = PROTO_SYNC;
    out[1] = type;
    out[2] = seq;
    out[3] = len;
    if (len) {
        memcpy(&out[4], payload, len);
    }
    uint16_t crc = crc16_ccitt_false(&out[1], (size_t)3 + len);
    out[4 + len] = (uint8_t)(crc & 0xFFu);
    out[5 + len] = (uint8_t)(crc >> 8);
    return (size_t)PROTO_HEADER_LEN + len + PROTO_CRC_LEN;
}
