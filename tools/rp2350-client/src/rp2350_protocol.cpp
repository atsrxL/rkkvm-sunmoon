// SPDX-License-Identifier: GPL-3.0-or-later
#include "rp2350_protocol.h"

#include <algorithm>

namespace rp2350 {

uint16_t crc16(const uint8_t *data, std::size_t length)
{
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

Bytes encodeFrame(uint8_t type, uint8_t seq, const Bytes &payload)
{
    if (payload.size() > kMaxPayload)
        return {};
    Bytes out;
    out.reserve(6 + payload.size());
    out.push_back(kSof);
    out.push_back(type);
    out.push_back(seq);
    out.push_back(static_cast<uint8_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    const uint16_t crc = crc16(out.data() + 1, out.size() - 1);
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    out.push_back(static_cast<uint8_t>(crc >> 8));
    return out;
}

static void putLe16(Bytes &b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>(v >> 8));
}

Bytes encodeHello(uint8_t seq) { return encodeFrame(Hello, seq, {kProtocolVersion}); }

Bytes encodeKeyboard(uint8_t seq, uint8_t modifiers, const uint8_t keys[6])
{
    Bytes p{modifiers};
    p.insert(p.end(), keys, keys + 6);
    return encodeFrame(Keyboard, seq, p);
}

Bytes encodeMouseRel(uint8_t seq, uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, int8_t pan)
{
    Bytes p{buttons};
    putLe16(p, static_cast<uint16_t>(dx));
    putLe16(p, static_cast<uint16_t>(dy));
    p.push_back(static_cast<uint8_t>(wheel));
    p.push_back(static_cast<uint8_t>(pan));
    return encodeFrame(MouseRel, seq, p);
}

Bytes encodeMouseAbs(uint8_t seq, uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel, int8_t pan)
{
    Bytes p{buttons};
    putLe16(p, std::min<uint16_t>(x, kAbsMax));
    putLe16(p, std::min<uint16_t>(y, kAbsMax));
    p.push_back(static_cast<uint8_t>(wheel));
    p.push_back(static_cast<uint8_t>(pan));
    return encodeFrame(MouseAbs, seq, p);
}

Bytes encodeReleaseAll(uint8_t seq) { return encodeFrame(ReleaseAll, seq, {}); }
Bytes encodePing(uint8_t seq) { return encodeFrame(Ping, seq, {}); }

void Decoder::reset()
{
    buffer_.clear();
    errors_ = 0;
}

std::vector<Frame> Decoder::feed(const uint8_t *data, std::size_t length)
{
    buffer_.insert(buffer_.end(), data, data + length);
    std::vector<Frame> frames;
    std::size_t pos = 0;
    while (pos < buffer_.size()) {
        if (buffer_[pos] != kSof) {
            ++pos;
            continue;
        }
        const std::size_t avail = buffer_.size() - pos;
        if (avail < 4)
            break;
        const std::size_t len = buffer_[pos + 3];
        if (len > kMaxPayload) {
            ++errors_;
            ++pos;
            continue;
        }
        if (avail < 6 + len)
            break;
        const uint8_t *body = buffer_.data() + pos + 1;
        const uint16_t got = static_cast<uint16_t>(body[3 + len] | (body[4 + len] << 8));
        if (crc16(body, 3 + len) != got) {
            ++errors_;
            ++pos;
            continue;
        }
        Frame f;
        f.type = body[0];
        f.seq = body[1];
        f.payload.assign(body + 3, body + 3 + len);
        frames.push_back(std::move(f));
        pos += 6 + len;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(pos));
    return frames;
}

bool parseInfo(const Frame &frame, InfoReply &out)
{
    if (frame.type != Info || frame.payload.size() != 12)
        return false;
    out.proto = frame.payload[0];
    out.fwMajor = frame.payload[1];
    out.fwMinor = frame.payload[2];
    out.caps = frame.payload[3];
    std::copy(frame.payload.begin() + 4, frame.payload.end(), out.serial);
    return true;
}

bool parsePong(const Frame &frame, PongReply &out)
{
    if (frame.type != Pong || frame.payload.size() != 5)
        return false;
    out.status = frame.payload[0];
    out.rxErrors = static_cast<uint16_t>(frame.payload[1] | (frame.payload[2] << 8));
    out.dropped = static_cast<uint16_t>(frame.payload[3] | (frame.payload[4] << 8));
    return true;
}

bool parseNak(const Frame &frame, NakReply &out)
{
    if (frame.type != Nak || frame.payload.size() != 2)
        return false;
    out.origType = frame.payload[0];
    out.reason = frame.payload[1];
    return true;
}

const char *nakReasonText(uint8_t reason)
{
    switch (reason) {
    case 1: return "CRC";
    case 2: return "length";
    case 3: return "unknown type";
    case 4: return "USB not ready";
    default: return "unknown";
    }
}

uint16_t scaleAbsolute(int position, int extent)
{
    if (extent <= 1 || position <= 0)
        return 0;
    if (position >= extent - 1)
        return kAbsMax;
    return static_cast<uint16_t>((static_cast<int64_t>(position) * kAbsMax + (extent - 1) / 2) / (extent - 1));
}

} // namespace rp2350

