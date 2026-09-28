// SPDX-License-Identifier: GPL-3.0-or-later
// RKMoon RP2350 HID bridge serial protocol v1 (docs/ADR-012-rp2350-hid-bridge.md).
// Pure encoding/decoding: no Qt GUI or serial dependency, reusable by the main client.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rp2350 {

constexpr uint8_t kSof = 0xA5;
constexpr uint8_t kProtocolVersion = 1;
constexpr std::size_t kMaxPayload = 32;
constexpr uint16_t kAbsMax = 32767;

enum Type : uint8_t {
    Hello = 0x01,
    Keyboard = 0x02,
    MouseRel = 0x03,
    MouseAbs = 0x04,
    ReleaseAll = 0x05,
    Ping = 0x06,
    Nak = 0x80,
    Info = 0x81,
    Pong = 0x86,
};

enum StatusBits : uint8_t {
    StatusUsbConfigured = 0x01,
    StatusHostSuspended = 0x02,
    StatusInputHeld = 0x04,
    StatusWatchdogFired = 0x08,
};

enum CapBits : uint8_t {
    CapKeyboard = 0x01,
    CapMouseRel = 0x02,
    CapMouseAbs = 0x04,
    CapRemoteWakeup = 0x08,
};

enum MouseButtons : uint8_t {
    ButtonLeft = 0x01,
    ButtonRight = 0x02,
    ButtonMiddle = 0x04,
    ButtonBack = 0x08,
    ButtonForward = 0x10,
};

using Bytes = std::vector<uint8_t>;

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor.
uint16_t crc16(const uint8_t *data, std::size_t length);

// Returns an empty vector if payload exceeds kMaxPayload.
Bytes encodeFrame(uint8_t type, uint8_t seq, const Bytes &payload);

Bytes encodeHello(uint8_t seq);
Bytes encodeKeyboard(uint8_t seq, uint8_t modifiers, const uint8_t keys[6]);
Bytes encodeMouseRel(uint8_t seq, uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, int8_t pan);
Bytes encodeMouseAbs(uint8_t seq, uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel, int8_t pan);
Bytes encodeReleaseAll(uint8_t seq);
Bytes encodePing(uint8_t seq);

struct Frame {
    uint8_t type = 0;
    uint8_t seq = 0;
    Bytes payload;
};

// Stream decoder. Skips non-SOF bytes while hunting; on len>32 or CRC failure it
// counts one error, drops only that SOF byte and rescans from the next byte.
class Decoder {
public:
    // Appends bytes and returns every complete valid frame now available.
    std::vector<Frame> feed(const uint8_t *data, std::size_t length);
    uint32_t errors() const { return errors_; }
    void reset();

private:
    Bytes buffer_;
    uint32_t errors_ = 0;
};

struct InfoReply {
    uint8_t proto = 0, fwMajor = 0, fwMinor = 0, caps = 0;
    uint8_t serial[8] = {};
};
struct PongReply {
    uint8_t status = 0;
    uint16_t rxErrors = 0, dropped = 0;
};
struct NakReply {
    uint8_t origType = 0, reason = 0;
};

bool parseInfo(const Frame &frame, InfoReply &out);
bool parsePong(const Frame &frame, PongReply &out);
bool parseNak(const Frame &frame, NakReply &out);

const char *nakReasonText(uint8_t reason);

// Maps a coordinate inside [0, extent) to 0..32767, clamping outside values.
uint16_t scaleAbsolute(int position, int extent);

} // namespace rp2350

