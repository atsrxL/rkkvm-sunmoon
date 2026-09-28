// SPDX-License-Identifier: GPL-3.0-or-later
// QSerialPort transport for the RP2350 HID bridge protocol.
#pragma once

#include "rp2350_protocol.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QSerialPort;

class SerialBridge : public QObject {
    Q_OBJECT
public:
    explicit SerialBridge(QObject *parent = nullptr);
    ~SerialBridge() override;

    bool open(const QString &portName, qint32 baud, QString *error);
    void close();
    bool isOpen() const;
    qint64 bytesPending() const;
    quint32 localDecodeErrors() const { return decoder_.errors(); }
    bool linkAlive() const { return linkAlive_; }

public slots:
    void sendHello();
    void sendKeyboard(quint8 modifiers, const QByteArray &keys);
    void sendMouseRel(quint8 buttons, int dx, int dy, int wheel, int pan);
    void sendMouseAbs(quint8 buttons, int x, int y, int wheel, int pan);
    void sendReleaseAll();

signals:
    void opened();
    void closed(const QString &reason);
    void infoReceived(const rp2350::InfoReply &info);
    void pongReceived(const rp2350::PongReply &pong, qint64 rttMs);
    void nakReceived(const rp2350::NakReply &nak);
    void linkChanged(bool alive);
    void logMessage(const QString &message);

private slots:
    void onReadyRead();
    void onPingTick();

private:
    void write(const rp2350::Bytes &frame);
    void setLinkAlive(bool alive);
    quint8 nextSeq() { return seq_++; }

    QSerialPort *port_ = nullptr;
    rp2350::Decoder decoder_;
    QTimer pingTimer_;
    QTimer helloRetry_;
    int helloAttempts_ = 0;
    bool haveInfo_ = false;
    quint8 seq_ = 0;
    int pingSeq_ = -1;
    QElapsedTimer pingSent_;
    QElapsedTimer lastReply_;
    bool linkAlive_ = false;
};

