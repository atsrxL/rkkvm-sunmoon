// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RKMOON_TUSB_CONFIG_H
#define RKMOON_TUSB_CONFIG_H

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined by the SDK
#endif

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_RPI_PIO_USB 0 // native controller only; see README (PIO-USB)

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_HID 3
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_HID_EP_BUFSIZE 8

#endif
