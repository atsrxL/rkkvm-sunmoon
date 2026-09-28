// SPDX-License-Identifier: GPL-3.0-or-later
#include "capturearea.h"

#include "hid_keymap.h"
#include "rp2350_protocol.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace rp2350;

namespace {
constexpr int kFlushMs = 4;
#ifdef Q_OS_WIN
CaptureArea *g_hookTarget = nullptr;
HHOOK g_hook = nullptr;

LRESULT CALLBACK lowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_hookTarget && g_hookTarget->captured()) {
        const auto *k = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        const HWND top = reinterpret_cast<HWND>(g_hookTarget->window()->winId());
        if (GetForegroundWindow() == top) {
            const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool extended = (k->flags & LLKHF_EXTENDED) != 0;
            if (g_hookTarget->handleNativeKey(k->scanCode, extended, k->vkCode, down))
                return 1; // swallowed: Win, Alt+Tab, etc. go only to the controlled machine
        }
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}
#endif
}

quint8 hidButtonsFromQt(Qt::MouseButtons b)
{
    quint8 out = 0;
    if (b & Qt::LeftButton) out |= ButtonLeft;
    if (b & Qt::RightButton) out |= ButtonRight;
    if (b & Qt::MiddleButton) out |= ButtonMiddle;
    if (b & Qt::BackButton) out |= ButtonBack;
    if (b & Qt::ForwardButton) out |= ButtonForward;
    return out;
}

CaptureArea::CaptureArea(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_OpaquePaintEvent);
    flushTimer_.setSingleShot(true);
    flushTimer_.setInterval(kFlushMs);
    connect(&flushTimer_, &QTimer::timeout, this, &CaptureArea::flushMotion);
}

CaptureArea::~CaptureArea() { removePlatformGrab(); }

void CaptureArea::setAbsoluteMode(bool absolute)
{
    if (absolute == absolute_)
        return;
    if (captured_)
        stopCapture(tr("切换鼠标模式"));
    absolute_ = absolute;
    update();
}

void CaptureArea::setEnabledForInput(bool enabled)
{
    inputEnabled_ = enabled;
    if (!enabled && captured_)
        stopCapture(tr("连接断开"));
    update();
}

void CaptureArea::startCapture()
{
    if (captured_ || !inputEnabled_)
        return;
    captured_ = true;
    keyboard_.clear();
    wheel_.clear();
    buttons_ = 0;
    ignoredButtons_ = hidButtonsFromQt(QGuiApplication::mouseButtons());
    pendingDx_ = pendingDy_ = 0;
    setFocus(Qt::MouseFocusReason);
    grabKeyboard();
    grabMouse();
    if (!absolute_) {
        setCursor(Qt::BlankCursor);
        QCursor::setPos(centerGlobal());
    } else {
        setCursor(Qt::CrossCursor);
    }
    installPlatformGrab();
    update();
    emit captureChanged(true, QString());
}

void CaptureArea::stopCapture(const QString &reason)
{
    if (!captured_)
        return;
    captured_ = false;
    flushTimer_.stop();
    removePlatformGrab();
    releaseKeyboard();
    releaseMouse();
    unsetCursor();
    keyboard_.clear();
    wheel_.clear();
    buttons_ = 0;
    ignoredButtons_ = 0;
    pendingDx_ = pendingDy_ = 0;
    absDirty_ = false;
    emit releaseAllRequested();
    update();
    emit captureChanged(false, reason);
}

void CaptureArea::installPlatformGrab()
{
#ifdef Q_OS_WIN
    g_hookTarget = this;
    if (!g_hook)
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    const QPoint tl = mapToGlobal(QPoint(0, 0));
    const qreal dpr = devicePixelRatioF();
    RECT r;
    r.left = static_cast<LONG>(tl.x() * dpr);
    r.top = static_cast<LONG>(tl.y() * dpr);
    r.right = static_cast<LONG>((tl.x() + width()) * dpr);
    r.bottom = static_cast<LONG>((tl.y() + height()) * dpr);
    ClipCursor(&r);
#endif
}

void CaptureArea::removePlatformGrab()
{
#ifdef Q_OS_WIN
    if (g_hookTarget == this) {
        if (g_hook)
            UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
        g_hookTarget = nullptr;
        ClipCursor(nullptr);
    }
#endif
}

bool CaptureArea::handleNativeKey(quint32 scanCode, bool extended, quint32 virtualKey, bool down)
{
    if (!captured_)
        return false;
    const uint8_t usage = hidUsageFromWindows(scanCode, extended, virtualKey);
    if (usage == 0)
        return true; // swallow unmapped / synthetic keys while captured
    if (down) {
        if (keyboard_.isReleaseChord(usage)) {
            stopCapture(tr("快捷键 %1").arg(QLatin1String(releaseChordText())));
            return true;
        }
        if (keyboard_.press(usage))
            emitKeyboard();
    } else if (keyboard_.release(usage)) {
        emitKeyboard();
    }
    return true;
}

void CaptureArea::emitKeyboard()
{
    uint8_t keys[6];
    keyboard_.keys(keys);
    emit keyboardReport(keyboard_.modifiers(), QByteArray(reinterpret_cast<const char *>(keys), 6));
}

static quint32 fallbackVirtualKey(const QKeyEvent *e)
{
    // Non-Windows development fallback: Qt key codes equal Windows VKs for A-Z, 0-9, space.
    const int k = e->key();
    if ((k >= Qt::Key_A && k <= Qt::Key_Z) || (k >= Qt::Key_0 && k <= Qt::Key_9) || k == Qt::Key_Space)
        return static_cast<quint32>(k);
    switch (k) {
    case Qt::Key_Shift: return vk::LShift;
    case Qt::Key_Control: return vk::LControl;
    case Qt::Key_Alt: return vk::LMenu;
    case Qt::Key_Meta: return vk::LWin;
    case Qt::Key_Return: return 0x0D;
    case Qt::Key_Escape: return 0x1B;
    case Qt::Key_Backspace: return 0x08;
    case Qt::Key_Tab: return 0x09;
    case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26;
    case Qt::Key_Right: return 0x27;
    case Qt::Key_Down: return 0x28;
    default: return 0;
    }
}

void CaptureArea::keyPressEvent(QKeyEvent *e)
{
    if (!captured_) {
        QWidget::keyPressEvent(e);
        return;
    }
    if (!e->isAutoRepeat()) {
#ifdef Q_OS_WIN
        handleNativeKey(e->nativeScanCode(), false, e->nativeVirtualKey(), true);
#else
        handleNativeKey(0, false, fallbackVirtualKey(e), true);
#endif
    }
    e->accept();
}

void CaptureArea::keyReleaseEvent(QKeyEvent *e)
{
    if (!captured_) {
        QWidget::keyReleaseEvent(e);
        return;
    }
    if (!e->isAutoRepeat()) {
#ifdef Q_OS_WIN
        handleNativeKey(e->nativeScanCode(), false, e->nativeVirtualKey(), false);
#else
        handleNativeKey(0, false, fallbackVirtualKey(e), false);
#endif
    }
    e->accept();
}

void CaptureArea::updateButtons(Qt::MouseButtons qtButtons)
{
    const quint8 raw = hidButtonsFromQt(qtButtons);
    ignoredButtons_ &= raw; // a button stops being ignored once it has been released
    const quint8 next = static_cast<quint8>(raw & ~ignoredButtons_);
    if (next == buttons_)
        return;
    flushMotion();
    buttons_ = next;
    sendMotionReport(0, 0);
}

void CaptureArea::mousePressEvent(QMouseEvent *e)
{
    if (!captured_) {
        if (e->button() == Qt::LeftButton && inputEnabled_)
            startCapture();
        e->accept();
        return;
    }
    if (absolute_) {
        absX_ = scaleAbsolute(qRound(e->position().x()), width());
        absY_ = scaleAbsolute(qRound(e->position().y()), height());
    }
    updateButtons(e->buttons());
    e->accept();
}

void CaptureArea::mouseReleaseEvent(QMouseEvent *e)
{
    if (captured_)
        updateButtons(e->buttons());
    e->accept();
}

QPoint CaptureArea::centerGlobal() const { return mapToGlobal(rect().center()); }

void CaptureArea::mouseMoveEvent(QMouseEvent *e)
{
    if (!captured_)
        return;
    if (absolute_) {
        absX_ = scaleAbsolute(qRound(e->position().x()), width());
        absY_ = scaleAbsolute(qRound(e->position().y()), height());
        absDirty_ = true;
    } else {
        const QPoint center = centerGlobal();
        const QPoint g = e->globalPosition().toPoint();
        if (g == center)
            return; // our own warp
        pendingDx_ += g.x() - center.x();
        pendingDy_ += g.y() - center.y();
        QCursor::setPos(center);
    }
    if (!flushTimer_.isActive())
        flushTimer_.start();
}

void CaptureArea::wheelEvent(QWheelEvent *e)
{
    if (!captured_) {
        e->ignore();
        return;
    }
    wheel_.add(e->angleDelta().x(), e->angleDelta().y());
    if (wheel_.pending()) {
        flushMotion();
        const auto [w, p] = wheel_.take();
        sendMotionReport(w, p);
    }
    e->accept();
}

void CaptureArea::flushMotion()
{
    if (!captured_)
        return;
    if (motionGate_ && !motionGate_()) {
        flushTimer_.start(); // serial backlog: keep accumulating
        return;
    }
    if (absolute_) {
        if (absDirty_)
            sendMotionReport(0, 0);
        return;
    }
    if (pendingDx_ == 0 && pendingDy_ == 0)
        return;
    for (const auto &[dx, dy] : splitRelative(pendingDx_, pendingDy_))
        emit mouseRelReport(buttons_, dx, dy, 0, 0);
    pendingDx_ = pendingDy_ = 0;
}

void CaptureArea::sendMotionReport(int wheel, int pan)
{
    if (absolute_) {
        absDirty_ = false;
        emit mouseAbsReport(buttons_, absX_, absY_, wheel, pan);
    } else {
        emit mouseRelReport(buttons_, 0, 0, wheel, pan);
    }
}

void CaptureArea::focusOutEvent(QFocusEvent *e)
{
    if (captured_ && e->reason() != Qt::PopupFocusReason)
        stopCapture(tr("失去焦点"));
    QWidget::focusOutEvent(e);
}

bool CaptureArea::event(QEvent *e)
{
    if (e->type() == QEvent::WindowDeactivate && captured_)
        stopCapture(tr("窗口失去焦点"));
    return QWidget::event(e);
}

void CaptureArea::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), captured_ ? QColor(24, 64, 40) : QColor(40, 40, 48));
    p.setPen(QPen(captured_ ? QColor(80, 220, 120) : QColor(120, 120, 140), 2));
    p.drawRect(rect().adjusted(1, 1, -2, -2));
    p.setPen(Qt::white);
    QString text;
    if (!inputEnabled_)
        text = tr("未连接");
    else if (captured_)
        text = tr("正在捕获（%1 模式）\n按 %2 释放").arg(absolute_ ? tr("绝对") : tr("相对"), QLatin1String(releaseChordText()));
    else
        text = tr("单击此处开始捕获键盘和鼠标\n（%1 模式）").arg(absolute_ ? tr("绝对：此区域映射到整个屏幕") : tr("相对"));
    p.drawText(rect(), Qt::AlignCenter, text);
}

