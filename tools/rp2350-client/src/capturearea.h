// SPDX-License-Identifier: GPL-3.0-or-later
// Capture area: grabs keyboard and mouse and turns them into HID reports.
// Never records or logs key contents.
#pragma once

#include "input_state.h"

#include <QByteArray>
#include <QPoint>
#include <QTimer>
#include <QWidget>
#include <functional>

class CaptureArea : public QWidget {
    Q_OBJECT
public:
    explicit CaptureArea(QWidget *parent = nullptr);
    ~CaptureArea() override;

    bool captured() const { return captured_; }
    bool absoluteMode() const { return absolute_; }
    void setAbsoluteMode(bool absolute);
    void setEnabledForInput(bool enabled);
    // Motion is held back (and accumulated) while this returns false.
    void setMotionGate(std::function<bool()> gate) { motionGate_ = std::move(gate); }

    // Platform-neutral key entry point (Windows hook / Qt key events / tests).
    // Returns true when the event was consumed by the capture.
    bool handleNativeKey(quint32 scanCode, bool extended, quint32 virtualKey, bool down);

    static const char *releaseChordText() { return "Ctrl+Alt+Shift+Z"; }

public slots:
    void startCapture();
    void stopCapture(const QString &reason);
    void flushMotion();

signals:
    void keyboardReport(quint8 modifiers, const QByteArray &keys);
    void mouseRelReport(quint8 buttons, int dx, int dy, int wheel, int pan);
    void mouseAbsReport(quint8 buttons, int x, int y, int wheel, int pan);
    void releaseAllRequested();
    void captureChanged(bool captured, const QString &reason);

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    bool focusNextPrevChild(bool) override { return !captured_; }

private:
    void emitKeyboard();
    void updateButtons(Qt::MouseButtons qtButtons);
    void sendMotionReport(int wheel, int pan);
    QPoint centerGlobal() const;
    void installPlatformGrab();
    void removePlatformGrab();

    rp2350::KeyboardState keyboard_;
    rp2350::WheelAccumulator wheel_;
    bool captured_ = false;
    bool absolute_ = false;
    bool inputEnabled_ = false;
    quint8 buttons_ = 0;
    quint8 ignoredButtons_ = 0;
    qint32 pendingDx_ = 0, pendingDy_ = 0;
    bool absDirty_ = false;
    int absX_ = 0, absY_ = 0;
    QTimer flushTimer_;
    std::function<bool()> motionGate_;
};

quint8 hidButtonsFromQt(Qt::MouseButtons buttons);

