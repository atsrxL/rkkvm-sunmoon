// SPDX-License-Identifier: GPL-3.0-or-later
// Composite HID: boot keyboard, relative mouse (boot-capable), absolute pointer.
#include "usb_descriptors.h"

#include <string.h>

#include "tusb.h"

#define EPNUM_KEYBOARD 0x81
#define EPNUM_MOUSE_REL 0x82
#define EPNUM_MOUSE_ABS 0x83
#define HID_POLL_MS 1

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = RKMOON_USB_VID,
    .idProduct = RKMOON_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

// Keyboard: standard boot layout (modifiers, reserved, 6 keys) + LED output.
static const uint8_t desc_hid_keyboard[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

// Relative mouse, report protocol: 5 buttons, X/Y int16, wheel int8, AC Pan
// int8 (7 bytes). In boot protocol the firmware sends the 3-byte boot report.
static const uint8_t desc_hid_mouse_rel[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_MOUSE),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
      HID_USAGE(HID_USAGE_DESKTOP_POINTER),
      HID_COLLECTION(HID_COLLECTION_PHYSICAL),
        HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
        HID_USAGE_MIN(1),
        HID_USAGE_MAX(5),
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX(1),
        HID_REPORT_COUNT(5),
        HID_REPORT_SIZE(1),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        HID_REPORT_COUNT(1),
        HID_REPORT_SIZE(3),
        HID_INPUT(HID_CONSTANT),
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
        HID_USAGE(HID_USAGE_DESKTOP_X),
        HID_USAGE(HID_USAGE_DESKTOP_Y),
        HID_LOGICAL_MIN_N(-32767, 2),
        HID_LOGICAL_MAX_N(32767, 2),
        HID_REPORT_SIZE(16),
        HID_REPORT_COUNT(2),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
        HID_USAGE(HID_USAGE_DESKTOP_WHEEL),
        HID_LOGICAL_MIN(0x81),
        HID_LOGICAL_MAX(0x7f),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(1),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
        HID_USAGE_PAGE(HID_USAGE_PAGE_CONSUMER),
        HID_USAGE_N(HID_USAGE_CONSUMER_AC_PAN, 2),
        HID_LOGICAL_MIN(0x81),
        HID_LOGICAL_MAX(0x7f),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(1),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
      HID_COLLECTION_END,
    HID_COLLECTION_END,
};

// Absolute pointer: 5 buttons, X/Y 0..32767, wheel, AC Pan (7 bytes).
static const uint8_t desc_hid_mouse_abs[] = {TUD_HID_REPORT_DESC_ABSMOUSE()};

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    switch (instance) {
    case ITF_NUM_KEYBOARD:
        return desc_hid_keyboard;
    case ITF_NUM_MOUSE_REL:
        return desc_hid_mouse_rel;
    default:
        return desc_hid_mouse_abs;
    }
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 3 * TUD_HID_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_keyboard), EPNUM_KEYBOARD,
                       CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
    TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE_REL, 5, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_mouse_rel), EPNUM_MOUSE_REL,
                       CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
    TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE_ABS, 6, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_mouse_abs), EPNUM_MOUSE_ABS,
                       CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static char serial_str[17] = "0000000000000000";

void usb_descriptors_set_serial(const uint8_t id[8]) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        serial_str[2 * i] = hex[id[i] >> 4];
        serial_str[2 * i + 1] = hex[id[i] & 0xF];
    }
    serial_str[16] = 0;
}

static const char *const string_desc[] = {
    NULL, // 0: language, handled below
    "RKMoon",
    "RKMoon HID Bridge",
    serial_str,
    "RKMoon Keyboard",
    "RKMoon Mouse",
    "RKMoon Absolute Pointer",
};

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;
    if (index == 0) {
        desc_str[1] = 0x0409;
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc) / sizeof(string_desc[0])) {
            return NULL;
        }
        const char *str = index == 3 ? serial_str : string_desc[index];
        chr_count = strlen(str);
        if (chr_count > 32) {
            chr_count = 32;
        }
        for (size_t i = 0; i < chr_count; i++) {
            desc_str[1 + i] = (uint16_t)(uint8_t)str[i];
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
