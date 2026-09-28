// SPDX-License-Identifier: GPL-3.0-or-later
// Windows keyboard input -> USB HID keyboard page usage (0x07).
#pragma once

#include <cstdint>

namespace rp2350 {

// Windows virtual-key codes needed by the translation (values from winuser.h).
namespace vk {
constexpr uint32_t Pause = 0x13, Snapshot = 0x2C, NumLock = 0x90;
constexpr uint32_t LShift = 0xA0, RShift = 0xA1, LControl = 0xA2, RControl = 0xA3, LMenu = 0xA4, RMenu = 0xA5;
constexpr uint32_t LWin = 0x5B, RWin = 0x5C, Apps = 0x5D;
}

// Returns true for the synthetic left-Ctrl that Windows injects ahead of AltGr
// (low-level hook: vk LControl with scan code 0x21D). Such events must be dropped.
bool isAltGrFakeControl(uint32_t scanCode, uint32_t virtualKey);

// scanCode: set-1 make code (0..0xFF); extended: the E0 prefix / LLKHF_EXTENDED flag.
// VK wins for the keys whose scan codes are ambiguous (Pause/NumLock/PrintScreen);
// the scan code wins otherwise so left/right modifiers and keypad/navigation keys are
// told apart independent of keyboard layout. Falls back to the VK when the scan code
// is unknown (e.g. injected input). Returns 0 when the key has no mapping.
uint8_t hidUsageFromWindows(uint32_t scanCode, bool extended, uint32_t virtualKey);

uint8_t hidUsageFromScanCode(uint32_t scanCode, bool extended);
uint8_t hidUsageFromVirtualKey(uint32_t virtualKey);

// HID usage for a lowercase ASCII letter/digit/space (used by the test string).
uint8_t hidUsageFromAscii(char c);

inline bool isModifierUsage(uint8_t usage) { return usage >= 0xE0 && usage <= 0xE7; }

} // namespace rp2350

