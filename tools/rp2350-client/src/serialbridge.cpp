// SPDX-License-Identifier: GPL-3.0-or-later
#include "serialbridge.h"

#include <QSerialPort>

using namespace rp2350;

namespace {
constexpr int kPingIntervalMs = 200;
constexpr int kLinkTimeoutMs = 1000;
constexpr int kHelloRetryMs = 500;
constexpr int kHelloAttempts = 6;
}

SerialBridge::SerialBridge(QObject *parent)
    : QObject(parent)
{
    pingTimer_.setInterval(kPingIntervalMs);
    connect(&pingTimer_, &QTimer::timeout, this, &SerialBridge::onPingTick);
    helloRetry_.setInterval(kHelloRetryMs);
    connect(&helloRetry_, &QTimer::timeout, this, [this] {
        if (haveInfo_ || ++helloAttempts_ >= kHelloAttempts) {
            helloRetry_.stop();
            if (!haveInfo_)
                emit logMessage(tr("HELLO 未收到 INFO 应答（已重试 %1 次）").arg(helloAttempts_));
            return;
        }
        sendHello();
    });
}

SerialBridge::~SerialBridge() { close(); }

bool SerialBridge::open(const QString &portName, qint32 baud, QString *error)
{
    close();
    port_ = new QSerialPort(this);
    port_->setPortName(portName);
    port_->setBaudRate(baud);
    port_->setDataBits(QSerialPort::Data8);
    port_->setParity(QSerialPort::NoParity);
    port_->setStopBits(QSerialPort::OneStop);
    port_->setFlowControl(QSerialPort::NoFlowControl);
    if (!port_->open(QIODevice::ReadWrite)) {
        if (error)
            *error = port_->errorString();
        delete port_;
        port_ = nullptr;
        return false;
    }
    // CDC ACM devices (PIO-USB channel) only forward data once DTR is asserted.
    port_->setDataTerminalReady(true);
    port_->clear();
    connect(port_, &QSerialPort::readyRead, this, &SerialBridge::onReadyRead);
    connect(port_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e == QSerialPort::ResourceError || e == QSerialPort::PermissionError) {
            const QString reason = port_ ? port_->errorString() : QString();
            QMetaObject::invokeMethod(this, [this, reason] {
                if (port_) {
                    port_->close();
                    port_->deleteLater();
                    port_ = nullptr;
                    pingTimer_.stop();
                    helloRetry_.stop();
                    setLinkAlive(false);
                    emit closed(tr("串口错误：%1").arg(reason));
                }
            }, Qt::QueuedConnection);
        }
    });
    decoder_.reset();
    haveInfo_ = false;
    helloAttempts_ = 0;
    pingSeq_ = -1;
    lastReply_.invalidate();
    emit opened();
    sendHello();
    helloRetry_.start();
    pingTimer_.start();
    return true;
}

void SerialBridge::close()
{
    if (!port_)
        return;
    pingTimer_.stop();
    helloRetry_.stop();
    sendReleaseAll();
    port_->waitForBytesWritten(100);
    port_->close();
    port_->deleteLater();
    port_ = nullptr;
    setLinkAlive(false);
    emit closed(tr("已断开"));
}

bool SerialBridge::isOpen() const { return port_ && port_->isOpen(); }

qint64 SerialBridge::bytesPending() const { return port_ ? port_->bytesToWrite() : 0; }

void SerialBridge::write(const Bytes &frame)
{
    if (!isOpen() || frame.empty())
        return;
    port_->write(reinterpret_cast<const char *>(frame.data()), static_cast<qint64>(frame.size()));
}

void SerialBridge::sendHello() { write(encodeHello(nextSeq())); }

void SerialBridge::sendKeyboard(quint8 modifiers, const QByteArray &keys)
{
    uint8_t k[6] = {};
    for (int i = 0; i < 6 && i < keys.size(); ++i)
        k[i] = static_cast<uint8_t>(keys.at(i));
    write(encodeKeyboard(nextSeq(), modifiers, k));
}

void SerialBridge::sendMouseRel(quint8 buttons, int dx, int dy, int wheel, int pan)
{
    write(encodeMouseRel(nextSeq(), buttons, static_cast<int16_t>(dx), static_cast<int16_t>(dy),
                         static_cast<int8_t>(wheel), static_cast<int8_t>(pan)));
}

void SerialBridge::sendMouseAbs(quint8 buttons, int x, int y, int wheel, int pan)
{
    write(encodeMouseAbs(nextSeq(), buttons, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                         static_cast<int8_t>(wheel), static_cast<int8_t>(pan)));
}

void SerialBridge::sendReleaseAll() { write(encodeReleaseAll(nextSeq())); }

void SerialBridge::onPingTick()
{
    if (!isOpen())
        return;
    if (lastReply_.isValid() ? lastReply_.elapsed() > kLinkTimeoutMs : (pingSent_.isValid() && pingSent_.elapsed() > kLinkTimeoutMs))
        setLinkAlive(false);
    pingSeq_ = nextSeq();
    pingSent_.start();
    write(encodePing(static_cast<uint8_t>(pingSeq_)));
}

void SerialBridge::setLinkAlive(bool alive)
{
    if (alive == linkAlive_)
        return;
    linkAlive_ = alive;
    emit linkChanged(alive);
}

void SerialBridge::onReadyRead()
{
    const QByteArray data = port_->readAll();
    const auto frames = decoder_.feed(reinterpret_cast<const uint8_t *>(data.constData()), static_cast<std::size_t>(data.size()));
    for (const Frame &f : frames) {
        lastReply_.start();
        setLinkAlive(true);
        if (InfoReply info; parseInfo(f, info)) {
            haveInfo_ = true;
            helloRetry_.stop();
            emit infoReceived(info);
        } else if (PongReply pong; parsePong(f, pong)) {
            const qint64 rtt = (f.seq == pingSeq_ && pingSent_.isValid()) ? pingSent_.elapsed() : -1;
            emit pongReceived(pong, rtt);
        } else if (NakReply nak; parseNak(f, nak)) {
            emit nakReceived(nak);
        } else {
            emit logMessage(tr("未知应答 type=0x%1 len=%2").arg(f.type, 2, 16, QLatin1Char('0')).arg(f.payload.size()));
        }
    }
}

