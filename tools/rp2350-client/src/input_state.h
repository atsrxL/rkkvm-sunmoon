// SPDX-License-Identifier: GPL-3.0-or-later
// Host-side keyboard / mouse report state. No Qt dependency.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace rp2350 {

class KeyboardState {
public:
    // Each returns true when the resulting report changed and must be sent.
    bool press(uint8_t usage);
    bool release(uint8_t usage);
    void clear();

    uint8_t modifiers() const { return modifiers_; }
    // Fills 6 key slots; more than 6 held non-modifier keys yields ErrorRollOver (0x01).
    void keys(uint8_t out[6]) const;
    bool anyHeld() const { return modifiers_ != 0 || !keys_.empty(); }

    // True if pressing 'usage' now completes Ctrl+Alt+Shift+Z (either side of each).
    bool isReleaseChord(uint8_t usage) const;

private:
    uint8_t modifiers_ = 0;
    std::vector<uint8_t> keys_; // non-modifier usages in press order
};

// Splits an accumulated relative motion into int16 steps (the firmware splits further
// into HID-report range).
std::vector<std::pair<int16_t, int16_t>> splitRelative(int32_t dx, int32_t dy);

// Converts wheel angle deltas (120 per notch) into whole detents, keeping remainders.
class WheelAccumulator {
public:
    // angleY: positive = away from user (scroll up). angleX: Qt convention, positive = left.
    void add(int angleX, int angleY);
    // Takes whole detents, clamped to int8; HID pan is positive to the right.
    std::pair<int8_t, int8_t> take(); // {wheel, pan}
    bool pending() const;
    void clear() { x_ = y_ = 0; }

private:
    int x_ = 0, y_ = 0;
};

} // namespace rp2350

