// SPDX-License-Identifier: GPL-3.0-or-later
// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0).
#ifndef RKMOON_CRC16_H
#define RKMOON_CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC16_CCITT_FALSE_INIT 0xFFFFu

uint16_t crc16_ccitt_false_update(uint16_t crc, const uint8_t *data, size_t len);
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
