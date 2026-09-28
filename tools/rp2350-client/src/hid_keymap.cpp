// SPDX-License-Identifier: GPL-3.0-or-later
#include "hid_keymap.h"

namespace rp2350 {

namespace {

// Index: set-1 make code 0x00..0x7F without E0 prefix.
const uint8_t kBase[0x80] = {
    /*00*/ 0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,
    /*10*/ 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16,
    /*20*/ 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, 0xE1, 0x31, 0x1D, 0x1B, 0x06, 0x19,
    /*30*/ 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x55, 0xE2, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
    /*40*/ 0x3F, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5F, 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59,
    /*50*/ 0x5A, 0x5B, 0x62, 0x63, 0x46, 0x00, 0x64, 0x44, 0x45, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /*60*/ 0x00, 0x00, 0x00, 0x00, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x00,
    /*70*/ 0x88, 0x00, 0x00, 0x87, 0x00, 0x00, 0x73, 0x00, 0x00, 0x8A, 0x00, 0x8B, 0x00, 0x89, 0x85, 0x00,
};

uint8_t extendedUsage(uint32_t sc)
{
    switch (sc) {
    case 0x1C: return 0x58; // keypad Enter
    case 0x1D: return 0xE4; // right Ctrl
    case 0x20: return 0x7F; // Mute
    case 0x2E: return 0x81; // Volume down
    case 0x30: return 0x80; // Volume up
    case 0x35: return 0x54; // keypad /
    case 0x36: return 0xE5; // right Shift (reported extended in some combinations)
    case 0x37: return 0x46; // Print Screen
    case 0x38: return 0xE6; // right Alt / AltGr
    case 0x45: return 0x53; // Num Lock
    case 0x46: return 0x48; // Ctrl+Break
    case 0x47: return 0x4A; // Home
    case 0x48: return 0x52; // Up
    case 0x49: return 0x4B; // Page Up
    case 0x4B: return 0x50; // Left
    case 0x4D: return 0x4F; // Right
    case 0x4F: return 0x4D; // End
    case 0x50: return 0x51; // Down
    case 0x51: return 0x4E; // Page Down
    case 0x52: return 0x49; // Insert
    case 0x53: return 0x4C; // Delete
    case 0x5B: return 0xE3; // left GUI
    case 0x5C: return 0xE7; // right GUI
    case 0x5D: return 0x65; // Application / Menu
    case 0x5E: return 0x66; // Power
    default: return 0x00;   // includes the fake E0 2A / E0 AA shift
    }
}

} // namespace

bool isAltGrFakeControl(uint32_t scanCode, uint32_t virtualKey)
{
    return (virtualKey == vk::LControl || virtualKey == 0x11) && (scanCode & 0x200) != 0;
}

uint8_t hidUsageFromScanCode(uint32_t scanCode, bool extended)
{
    // Accept the Qt/Windows "nativeScanCode" form where bit 8 is the extended flag.
    if (scanCode & 0x100)
        extended = true;
    const uint32_t sc = scanCode & 0xFF;
    if (extended)
        return extendedUsage(sc);
    if (sc < 0x80)
        return kBase[sc];
    if (sc == 0xF1)
        return 0x91; // LANG2 (Hanja)
    if (sc == 0xF2)
        return 0x90; // LANG1 (Hangul)
    return 0x00;
}

uint8_t hidUsageFromVirtualKey(uint32_t v)
{
    if (v >= 'A' && v <= 'Z')
        return static_cast<uint8_t>(0x04 + (v - 'A'));
    if (v >= '1' && v <= '9')
        return static_cast<uint8_t>(0x1E + (v - '1'));
    if (v == '0')
        return 0x27;
    if (v >= 0x70 && v <= 0x7B) // F1..F12
        return static_cast<uint8_t>(0x3A + (v - 0x70));
    if (v >= 0x7C && v <= 0x87) // F13..F24
        return static_cast<uint8_t>(0x68 + (v - 0x7C));
    if (v >= 0x60 && v <= 0x69) // numpad 0..9
        return v == 0x60 ? 0x62 : static_cast<uint8_t>(0x59 + (v - 0x61));
    switch (v) {
    case 0x08: return 0x2A; // Backspace
    case 0x09: return 0x2B; // Tab
    case 0x0D: return 0x28; // Enter
    case 0x1B: return 0x29; // Escape
    case 0x20: return 0x2C; // Space
    case 0x21: return 0x4B; // Page Up
    case 0x22: return 0x4E; // Page Down
    case 0x23: return 0x4D; // End
    case 0x24: return 0x4A; // Home
    case 0x25: return 0x50; // Left
    case 0x26: return 0x52; // Up
    case 0x27: return 0x4F; // Right
    case 0x28: return 0x51; // Down
    case 0x2D: return 0x49; // Insert
    case 0x2E: return 0x4C; // Delete
    case 0x14: return 0x39; // Caps Lock
    case 0x91: return 0x47; // Scroll Lock
    case vk::Pause: return 0x48;
    case vk::Snapshot: return 0x46;
    case vk::NumLock: return 0x53;
    case vk::LWin: return 0xE3;
    case vk::RWin: return 0xE7;
    case vk::Apps: return 0x65;
    case vk::LShift: return 0xE1;
    case vk::RShift: return 0xE5;
    case vk::LControl: return 0xE0;
    case vk::RControl: return 0xE4;
    case vk::LMenu: return 0xE2;
    case vk::RMenu: return 0xE6;
    case 0x10: return 0xE1; // generic Shift -> left
    case 0x11: return 0xE0; // generic Ctrl -> left
    case 0x12: return 0xE2; // generic Alt -> left
    case 0x6A: return 0x55; // numpad *
    case 0x6B: return 0x57; // numpad +
    case 0x6D: return 0x56; // numpad -
    case 0x6E: return 0x63; // numpad .
    case 0x6F: return 0x54; // numpad /
    case 0xAD: return 0x7F; // Volume mute
    case 0xAE: return 0x81; // Volume down
    case 0xAF: return 0x80; // Volume up
    case 0xBA: return 0x33; // ;
    case 0xBB: return 0x2E; // =
    case 0xBC: return 0x36; // ,
    case 0xBD: return 0x2D; // -
    case 0xBE: return 0x37; // .
    case 0xBF: return 0x38; // /
    case 0xC0: return 0x35; // grave
    case 0xDB: return 0x2F; // [
    case 0xDC: return 0x31; // backslash
    case 0xDD: return 0x30; // ]
    case 0xDE: return 0x34; // quote
    case 0xE2: return 0x64; // non-US backslash
    default: return 0x00;
    }
}

uint8_t hidUsageFromWindows(uint32_t scanCode, bool extended, uint32_t virtualKey)
{
    switch (virtualKey) {
    case vk::Pause: return 0x48;
    case vk::NumLock: return 0x53;
    case vk::Snapshot: return 0x46;
    default: break;
    }
    if (isAltGrFakeControl(scanCode, virtualKey))
        return 0x00;
    const uint8_t fromScan = hidUsageFromScanCode(scanCode & 0x1FF, extended);
    if (fromScan != 0)
        return fromScan;
    // The E0 2A/E0 AA fake shift has a real scan code but no key: do not fall back.
    if ((scanCode & 0xFF) == 0x2A && (extended || (scanCode & 0x100)))
        return 0x00;
    return hidUsageFromVirtualKey(virtualKey);
}

uint8_t hidUsageFromAscii(char c)
{
    if (c >= 'a' && c <= 'z')
        return static_cast<uint8_t>(0x04 + (c - 'a'));
    if (c >= '1' && c <= '9')
        return static_cast<uint8_t>(0x1E + (c - '1'));
    if (c == '0')
        return 0x27;
    if (c == ' ')
        return 0x2C;
    return 0x00;
}

} // namespace rp2350

