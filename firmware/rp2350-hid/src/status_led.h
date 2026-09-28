// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RKMOON_STATUS_LED_H
#define RKMOON_STATUS_LED_H

#include <stdbool.h>

#include "bridge.h"

bool status_led_init(void);
void status_led_set(bridge_led_t color); // writes only on change

#endif
