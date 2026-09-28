// SPDX-License-Identifier: GPL-3.0-or-later
// Offline unit tests. Protocol cases are driven by docs/rp2350-protocol-vectors.json,
// shared with the firmware. RP2350_WITH_GUI adds capture/window tests (needs
// QtSerialPort; always built on Windows).
#include "hid_keymap.h"
#include "input_state.h"
#include "rp2350_protocol.h"

#ifdef RP2350_WITH_GUI
#include "capturearea.h"
#include "mainwindow.h"
#include "serialbridge.h"
#include <QSignalSpy>
#endif

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace rp2350;

static QByteArray toQ(const Bytes &b) { return QByteArray(reinterpret_cast<const char *>(b.data()), static_cast<int>(b.size())); }
static Bytes fromHex(const QString &h)
{
    const QByteArray raw = QByteArray::fromHex(h.toLatin1());
    return Bytes(raw.begin(), raw.end());
}

class TestRp2350 : public QObject {
    Q_OBJECT
    QJsonObject vectors_;

    QJsonArray frames() const { return vectors_.value("frames").toArray(); }

private slots:
    void initTestCase()
    {
        QFile f(QStringLiteral(RP2350_VECTORS_FILE));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
        vectors_ = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(vectors_.value("protocol").toInt(), 1);
        QCOMPARE(vectors_.value("max_payload").toInt(), int(kMaxPayload));
        QVERIFY(frames().size() >= 10);
    }

    void crcCheckValue()
    {
        const QJsonObject c = vectors_.value("crc").toObject();
        const QByteArray ascii = c.value("check_ascii").toString().toLatin1();
        bool ok = false;
        const uint16_t expected = static_cast<uint16_t>(c.value("check").toString().toUInt(&ok, 16));
        QVERIFY(ok);
        QCOMPARE(crc16(reinterpret_cast<const uint8_t *>(ascii.constData()), ascii.size()), expected);
        QCOMPARE(expected, uint16_t(0x29B1));
    }

    void encodeMatchesVectors()
    {
        int checked = 0;
        for (const auto v : frames()) {
            const QJsonObject o = v.toObject();
            const QString name = o.value("name").toString();
            const auto t = static_cast<uint8_t>(o.value("type").toInt());
            const auto seq = static_cast<uint8_t>(o.value("seq").toInt());
            const QJsonObject fl = o.value("fields").toObject();
            const QByteArray expected = QByteArray::fromHex(o.value("frame_hex").toString().toLatin1());
            QCOMPARE(toQ(encodeFrame(t, seq, fromHex(o.value("payload_hex").toString()))), expected);
            Bytes typed;
            switch (t) {
            case Hello: typed = encodeHello(seq); break;
            case Keyboard: {
                uint8_t k[6];
                const QJsonArray a = fl.value("keys").toArray();
                for (int i = 0; i < 6; ++i) k[i] = static_cast<uint8_t>(a.at(i).toInt());
                typed = encodeKeyboard(seq, static_cast<uint8_t>(fl.value("modifiers").toInt()), k);
                break;
            }
            case MouseRel:
                typed = encodeMouseRel(seq, fl.value("buttons").toInt(), fl.value("dx").toInt(), fl.value("dy").toInt(),
                                       fl.value("wheel").toInt(), fl.value("pan").toInt());
                break;
            case MouseAbs:
                typed = encodeMouseAbs(seq, fl.value("buttons").toInt(), fl.value("x").toInt(), fl.value("y").toInt(),
                                       fl.value("wheel").toInt(), fl.value("pan").toInt());
                break;
            case ReleaseAll: typed = encodeReleaseAll(seq); break;
            case Ping: typed = encodePing(seq); break;
            default: continue; // board -> host: covered by decode test
            }
            QVERIFY2(toQ(typed) == expected, qPrintable(name));
            ++checked;
        }
        QCOMPARE(checked, 10);
    }

    void decodeRepliesFromVectors()
    {
        int replies = 0;
        for (const auto v : frames()) {
            const QJsonObject o = v.toObject();
            const Bytes raw = fromHex(o.value("frame_hex").toString());
            Decoder d;
            const auto out = d.feed(raw.data(), raw.size());
            QCOMPARE(int(out.size()), 1);
            QCOMPARE(d.errors(), 0u);
            QCOMPARE(int(out[0].type), o.value("type").toInt());
            QCOMPARE(int(out[0].seq), o.value("seq").toInt());
            const QJsonObject fl = o.value("fields").toObject();
            if (out[0].type == Info) {
                InfoReply i;
                QVERIFY(parseInfo(out[0], i));
                QCOMPARE(int(i.proto), fl.value("proto").toInt());
                QCOMPARE(int(i.fwMajor), fl.value("fw_major").toInt());
                QCOMPARE(int(i.fwMinor), fl.value("fw_minor").toInt());
                QCOMPARE(int(i.caps), fl.value("caps").toInt());
                QCOMPARE(QByteArray(reinterpret_cast<const char *>(i.serial), 8).toHex(), fl.value("serial_hex").toString().toLatin1());
                ++replies;
            } else if (out[0].type == Pong) {
                PongReply p;
                QVERIFY(parsePong(out[0], p));
                QCOMPARE(int(p.status), fl.value("status").toInt());
                QCOMPARE(int(p.rxErrors), fl.value("rx_errors").toInt());
                QCOMPARE(int(p.dropped), fl.value("dropped").toInt());
                ++replies;
            } else if (out[0].type == Nak) {
                NakReply n;
                QVERIFY(parseNak(out[0], n));
                QCOMPARE(int(n.origType), fl.value("orig_type").toInt());
                QCOMPARE(int(n.reason), fl.value("reason").toInt());
                ++replies;
            }
        }
        QCOMPARE(replies, 5);
    }

    void decodeStreamsFromVectors()
    {
        const QJsonArray streams = vectors_.value("decode_streams").toArray();
        QVERIFY(streams.size() >= 5);
        for (const auto v : streams) {
            const QJsonObject o = v.toObject();
            const Bytes data = fromHex(o.value("stream_hex").toString());
            // Whole buffer at once, and byte by byte, must agree.
            for (int mode = 0; mode < 2; ++mode) {
                Decoder d;
                std::vector<Frame> out;
                if (mode == 0) {
                    out = d.feed(data.data(), data.size());
                } else {
                    for (uint8_t b : data)
                        for (auto &f : d.feed(&b, 1)) out.push_back(f);
                }
                const QJsonArray exp = o.value("expect").toArray();
                QVERIFY2(int(out.size()) == exp.size(), qPrintable(o.value("name").toString()));
                for (int i = 0; i < exp.size(); ++i) {
                    const QJsonObject e = exp.at(i).toObject();
                    QCOMPARE(int(out[i].type), e.value("type").toInt());
                    QCOMPARE(int(out[i].seq), e.value("seq").toInt());
                    QCOMPARE(toQ(out[i].payload).toHex(), e.value("payload_hex").toString().toLatin1());
                }
                QCOMPARE(int(d.errors()), o.value("rx_errors").toInt());
            }
        }
    }

    void oversizedPayloadRejected()
    {
        QVERIFY(encodeFrame(0x02, 0, Bytes(33, 0)).empty());
        QCOMPARE(int(encodeFrame(0x02, 0, Bytes(32, 0)).size()), 38);
        QCOMPARE(scaleAbsolute(-5, 100), uint16_t(0));
        QCOMPARE(scaleAbsolute(0, 100), uint16_t(0));
        QCOMPARE(scaleAbsolute(99, 100), uint16_t(32767));
        QCOMPARE(scaleAbsolute(500, 100), uint16_t(32767));
        QCOMPARE(scaleAbsolute(50, 101), uint16_t(16384));
        QCOMPARE(scaleAbsolute(10, 1), uint16_t(0));
    }

    void scanCodeModifiersLeftRight()
    {
        QCOMPARE(hidUsageFromWindows(0x1D, false, vk::LControl), uint8_t(0xE0));
        QCOMPARE(hidUsageFromWindows(0x1D, true, vk::RControl), uint8_t(0xE4));
        QCOMPARE(hidUsageFromWindows(0x2A, false, vk::LShift), uint8_t(0xE1));
        QCOMPARE(hidUsageFromWindows(0x36, false, vk::RShift), uint8_t(0xE5));
        QCOMPARE(hidUsageFromWindows(0x38, false, vk::LMenu), uint8_t(0xE2));
        QCOMPARE(hidUsageFromWindows(0x38, true, vk::RMenu), uint8_t(0xE6));
        QCOMPARE(hidUsageFromWindows(0x5B, true, vk::LWin), uint8_t(0xE3));
        QCOMPARE(hidUsageFromWindows(0x5C, true, vk::RWin), uint8_t(0xE7));
        // Qt nativeScanCode form: bit 8 = extended
        QCOMPARE(hidUsageFromWindows(0x11D, false, vk::RControl), uint8_t(0xE4));
        QCOMPARE(hidUsageFromWindows(0x138, false, vk::RMenu), uint8_t(0xE6));
        // AltGr's injected fake left Ctrl is dropped
        QVERIFY(isAltGrFakeControl(0x21D, vk::LControl));
        QCOMPARE(hidUsageFromWindows(0x21D, false, vk::LControl), uint8_t(0));
        // Fake shift (E0 2A) produces nothing
        QCOMPARE(hidUsageFromWindows(0x2A, true, 0xFF), uint8_t(0));
    }

    void scanCodeKeysAndKeypad()
    {
        QCOMPARE(hidUsageFromWindows(0x1E, false, 'A'), uint8_t(0x04));
        QCOMPARE(hidUsageFromWindows(0x2C, false, 'Z'), uint8_t(0x1D));
        QCOMPARE(hidUsageFromWindows(0x10, false, 'A'), uint8_t(0x14)); // AZERTY: physical Q position
        QCOMPARE(hidUsageFromWindows(0x1C, false, 0x0D), uint8_t(0x28));
        QCOMPARE(hidUsageFromWindows(0x1C, true, 0x0D), uint8_t(0x58));
        QCOMPARE(hidUsageFromWindows(0x48, false, 0x68), uint8_t(0x60)); // keypad 8
        QCOMPARE(hidUsageFromWindows(0x48, true, 0x26), uint8_t(0x52)); // arrow up
        QCOMPARE(hidUsageFromWindows(0x53, true, 0x2E), uint8_t(0x4C)); // Delete
        QCOMPARE(hidUsageFromWindows(0x53, false, 0x6E), uint8_t(0x63)); // keypad .
        QCOMPARE(hidUsageFromWindows(0x35, true, 0x6F), uint8_t(0x54)); // keypad /
        QCOMPARE(hidUsageFromWindows(0x45, false, vk::Pause), uint8_t(0x48));
        QCOMPARE(hidUsageFromWindows(0x45, true, vk::NumLock), uint8_t(0x53));
        QCOMPARE(hidUsageFromWindows(0x37, true, vk::Snapshot), uint8_t(0x46));
        QCOMPARE(hidUsageFromWindows(0x57, false, 0x7A), uint8_t(0x44)); // F11
        QCOMPARE(hidUsageFromWindows(0x58, false, 0x7B), uint8_t(0x45)); // F12
        QCOMPARE(hidUsageFromWindows(0x5D, true, vk::Apps), uint8_t(0x65));
        QCOMPARE(hidUsageFromWindows(0x56, false, 0xE2), uint8_t(0x64));
        QCOMPARE(hidUsageFromWindows(0x29, false, 0xC0), uint8_t(0x35));
        // unknown scan code falls back to VK (e.g. injected input)
        QCOMPARE(hidUsageFromWindows(0, false, 'Q'), uint8_t(0x14));
        QCOMPARE(hidUsageFromWindows(0, false, vk::RShift), uint8_t(0xE5));
        QCOMPARE(hidUsageFromAscii('r'), uint8_t(0x15));
        QCOMPARE(hidUsageFromAscii('k'), uint8_t(0x0E));
    }

    void keyboardStateReports()
    {
        KeyboardState k;
        QVERIFY(k.press(0xE0));
        QVERIFY(!k.press(0xE0));
        QVERIFY(k.press(0xE5));
        QCOMPARE(int(k.modifiers()), 0x21);
        for (uint8_t u = 0x04; u < 0x0A; ++u) QVERIFY(k.press(u));
        uint8_t keys[6];
        k.keys(keys);
        QCOMPARE(int(keys[0]), 0x04);
        QCOMPARE(int(keys[5]), 0x09);
        QVERIFY(!k.press(0x04)); // repeat
        QVERIFY(k.press(0x0A));  // 7th key -> rollover
        k.keys(keys);
        for (uint8_t b : keys) QCOMPARE(int(b), 0x01);
        QVERIFY(k.release(0x04));
        k.keys(keys);
        QCOMPARE(int(keys[0]), 0x05);
        QCOMPARE(int(keys[5]), 0x0A);
        QVERIFY(!k.release(0x77));
        QVERIFY(k.release(0xE0));
        QCOMPARE(int(k.modifiers()), 0x20);
        QVERIFY(k.anyHeld());
        k.clear();
        QVERIFY(!k.anyHeld());
    }

    void releaseChordNeedsAllThreeModifiers()
    {
        KeyboardState k;
        k.press(0xE0); k.press(0xE2);
        QVERIFY(!k.isReleaseChord(0x1D));
        k.press(0xE5); // right shift is fine
        QVERIFY(k.isReleaseChord(0x1D));
        QVERIFY(!k.isReleaseChord(0x1C));
        KeyboardState r;
        r.press(0xE4); r.press(0xE6); r.press(0xE1);
        QVERIFY(r.isReleaseChord(0x1D));
    }

    void relativeSplitAndWheel()
    {
        auto s = splitRelative(70000, -5);
        QCOMPARE(int(s.size()), 3);
        int32_t sx = 0, sy = 0;
        for (auto &p : s) { sx += p.first; sy += p.second; }
        QCOMPARE(sx, 70000);
        QCOMPARE(sy, -5);
        QVERIFY(splitRelative(0, 0).empty());
        auto n = splitRelative(-40000, 0);
        QCOMPARE(int(n[0].first), -32768);

        WheelAccumulator w;
        w.add(0, 60);
        QVERIFY(!w.pending());
        w.add(0, 60);
        QVERIFY(w.pending());
        auto [wh, pan] = w.take();
        QCOMPARE(int(wh), 1);
        QCOMPARE(int(pan), 0);
        w.add(-240, -120); // Qt: negative x = scroll right
        auto [wh2, pan2] = w.take();
        QCOMPARE(int(wh2), -1);
        QCOMPARE(int(pan2), 2);
        w.add(0, 120 * 500);
        QCOMPARE(int(w.take().first), 127);
    }

#ifdef RP2350_WITH_GUI
    void captureAreaKeyboardAndReleaseChord()
    {
        CaptureArea a;
        a.resize(400, 300);
        a.show();
        a.setEnabledForInput(true);
        QSignalSpy kb(&a, &CaptureArea::keyboardReport);
        QSignalSpy rel(&a, &CaptureArea::releaseAllRequested);
        QSignalSpy cap(&a, &CaptureArea::captureChanged);
        QVERIFY(!a.handleNativeKey(0x1E, false, 'A', true)); // not captured: ignored
        a.startCapture();
        QVERIFY(a.captured());
        QVERIFY(a.handleNativeKey(0x1D, true, vk::RControl, true));
        QCOMPARE(kb.size(), 1);
        QCOMPARE(int(kb.last().at(0).value<quint8>()), 0x10);
        QVERIFY(a.handleNativeKey(0x1D, true, vk::RControl, false));
        QCOMPARE(int(kb.last().at(0).value<quint8>()), 0x00);
        a.handleNativeKey(0x1D, false, vk::LControl, true);
        a.handleNativeKey(0x38, false, vk::LMenu, true);
        a.handleNativeKey(0x2A, false, vk::LShift, true);
        const int before = kb.size();
        a.handleNativeKey(0x2C, false, 'Z', true);
        QVERIFY(!a.captured());
        QCOMPARE(kb.size(), before); // Z itself never sent
        QCOMPARE(rel.size(), 1);
        QCOMPARE(cap.last().at(0).toBool(), false);
    }

    void captureAreaStopsWhenDisconnected()
    {
        CaptureArea a;
        a.show();
        a.setEnabledForInput(true);
        QSignalSpy rel(&a, &CaptureArea::releaseAllRequested);
        a.startCapture();
        a.setEnabledForInput(false);
        QVERIFY(!a.captured());
        QCOMPARE(rel.size(), 1);
        a.startCapture();
        QVERIFY(!a.captured());
    }

    void captureAreaAbsoluteMapping()
    {
        CaptureArea a;
        a.resize(401, 201); // above the 320x180 minimum size
        a.setAbsoluteMode(true);
        a.show();
        QCOMPARE(a.width(), 401);
        QCOMPARE(a.height(), 201);
        a.setEnabledForInput(true);
        a.startCapture();
        QSignalSpy abs(&a, &CaptureArea::mouseAbsReport);
        QMouseEvent move(QEvent::MouseMove, QPointF(400, 100), a.mapToGlobal(QPointF(400, 100)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&a, &move);
        a.flushMotion();
        QCOMPARE(abs.size(), 1);
        QCOMPARE(abs.last().at(1).toInt(), 32767);
        QCOMPARE(abs.last().at(2).toInt(), 16384);
        QWheelEvent wheel(QPointF(10, 10), a.mapToGlobal(QPointF(10, 10)), QPoint(), QPoint(-120, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&a, &wheel);
        QCOMPARE(abs.last().at(3).toInt(), 1);
        QCOMPARE(abs.last().at(4).toInt(), 1);
        a.stopCapture(QStringLiteral("test"));
    }

    void formatStatusText()
    {
        PongReply p;
        p.status = StatusUsbConfigured | StatusWatchdogFired;
        p.rxErrors = 3;
        p.dropped = 7;
        const QString s = MainWindow::formatPong(p, 4);
        QVERIFY(s.contains(QStringLiteral("USB 已配置：是")));
        QVERIFY(s.contains(QStringLiteral("被控机挂起：否")));
        QVERIFY(s.contains(QStringLiteral("看门狗触发过：是")));
        QVERIFY(s.contains(QStringLiteral("接收错误：3")));
        InfoReply i;
        i.proto = 1; i.fwMajor = 0; i.fwMinor = 1; i.caps = 0x0F;
        const uint8_t serial[8] = {0xE6, 0x61, 0x41, 0x03, 0xE7, 0x45, 0x2D, 0x2F};
        std::copy(serial, serial + 8, i.serial);
        const QString t = MainWindow::formatInfo(i);
        QVERIFY(t.contains(QStringLiteral("固件 0.1")));
        QVERIFY(t.contains(QStringLiteral("E6614103E7452D2F")));
        QVERIFY(t.contains(QStringLiteral("远程唤醒")));
    }

    void mainWindowStartsDisconnected()
    {
        MainWindow w;
        QVERIFY(!w.bridge()->isOpen());
        w.captureArea()->startCapture();
        QVERIFY(!w.captureArea()->captured());
    }
#endif
};

QTEST_MAIN(TestRp2350)
#include "tst_rp2350.moc"
