// SPDX-License-Identifier: GPL-3.0-or-later
#include "status_led.h"

#include "hardware/pio.h"
#include "ws2812.pio.h"

// Low brightness: the LED sits next to the USB port and is viewed up close.
#define LVL 24u

static PIO led_pio;
static uint led_sm;
static bool led_ok;
static int last = -1;

bool status_led_init(void) {
    uint offset;
    led_ok = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &led_pio, &led_sm, &offset,
                                                              RKMOON_WS2812_PIN, 1, true);
    if (led_ok) {
        ws2812_program_init(led_pio, led_sm, offset, RKMOON_WS2812_PIN, 800000.0f);
    }
    return led_ok;
}

static uint32_t grb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
}

void status_led_set(bridge_led_t color) {
    if (!led_ok || (int)color == last || pio_sm_is_tx_fifo_full(led_pio, led_sm)) {
        return;
    }
    uint32_t v;
    switch (color) {
    case BRIDGE_LED_GREEN:
        v = grb(0, LVL, 0);
        break;
    case BRIDGE_LED_BLUE:
        v = grb(0, 0, LVL);
        break;
    case BRIDGE_LED_YELLOW:
        v = grb(LVL, LVL, 0);
        break;
    case BRIDGE_LED_RED:
    default:
        v = grb(LVL, 0, 0);
        break;
    }
    pio_sm_put(led_pio, led_sm, v << 8u);
    last = (int)color;
}
