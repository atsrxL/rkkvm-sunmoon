// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RKMOON_USB_DESCRIPTORS_H
#define RKMOON_USB_DESCRIPTORS_H

#include <stdint.h>

// HID instance numbers == interface numbers == bridge_iface_t values.
enum {
    ITF_NUM_KEYBOARD = 0,
    ITF_NUM_MOUSE_REL = 1,
    ITF_NUM_MOUSE_ABS = 2,
    ITF_NUM_TOTAL
};

void usb_descriptors_set_serial(const uint8_t id[8]);

#endif
