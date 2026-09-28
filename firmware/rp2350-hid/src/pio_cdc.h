// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RKMOON_PIO_CDC_H
#define RKMOON_PIO_CDC_H
#include "bridge.h"
void pio_cdc_init(const uint8_t serial[8]);
void pio_cdc_pump(bridge_t *bridge, uint32_t now);
bool pio_cdc_write(const uint8_t *data, size_t len);
#endif
