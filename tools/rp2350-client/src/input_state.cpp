// SPDX-License-Identifier: GPL-3.0-or-later
#include "input_state.h"

#include "hid_keymap.h"

#include <algorithm>
#include <cstdlib>

namespace rp2350 {

bool KeyboardState::press(uint8_t usage)
{
    if (usage == 0)
        return false;
    if (isModifierUsage(usage)) {
        const uint8_t bit = static_cast<uint8_t>(1u << (usage - 0xE0));
        if (modifiers_ & bit)
            return false;
        modifiers_ |= bit;
        return true;
    }
    if (std::find(keys_.begin(), keys_.end(), usage) != keys_.end())
        return false; // auto-repeat
    keys_.push_back(usage);
    return true;
}

bool KeyboardState::release(uint8_t usage)
{
    if (usage == 0)
        return false;
    if (isModifierUsage(usage)) {
        const uint8_t bit = static_cast<uint8_t>(1u << (usage - 0xE0));
        if (!(modifiers_ & bit))
            return false;
        modifiers_ &= static_cast<uint8_t>(~bit);
        return true;
    }
    auto it = std::find(keys_.begin(), keys_.end(), usage);
    if (it == keys_.end())
        return false;
    keys_.erase(it);
    return true;
}

void KeyboardState::clear()
{
    modifiers_ = 0;
    keys_.clear();
}

void KeyboardState::keys(uint8_t out[6]) const
{
    if (keys_.size() > 6) {
        std::fill(out, out + 6, uint8_t{0x01});
        return;
    }
    std::fill(out, out + 6, uint8_t{0});
    std::copy(keys_.begin(), keys_.end(), out);
}

bool KeyboardState::isReleaseChord(uint8_t usage) const
{
    return usage == 0x1D && (modifiers_ & 0x11) && (modifiers_ & 0x22) && (modifiers_ & 0x44);
}

std::vector<std::pair<int16_t, int16_t>> splitRelative(int32_t dx, int32_t dy)
{
    std::vector<std::pair<int16_t, int16_t>> out;
    auto step = [](int32_t &v) {
        const int32_t s = std::clamp<int32_t>(v, -32768, 32767);
        v -= s;
        return static_cast<int16_t>(s);
    };
    while (dx != 0 || dy != 0) {
        const int16_t sx = step(dx);
        const int16_t sy = step(dy);
        out.emplace_back(sx, sy);
    }
    return out;
}

void WheelAccumulator::add(int angleX, int angleY)
{
    x_ += angleX;
    y_ += angleY;
}

bool WheelAccumulator::pending() const
{
    return std::abs(x_) >= 120 || std::abs(y_) >= 120;
}

std::pair<int8_t, int8_t> WheelAccumulator::take()
{
    const int wheel = std::clamp(y_ / 120, -127, 127);
    const int left = std::clamp(x_ / 120, -127, 127);
    y_ -= wheel * 120;
    x_ -= left * 120;
    return {static_cast<int8_t>(wheel), static_cast<int8_t>(-left)};
}

} // namespace rp2350

