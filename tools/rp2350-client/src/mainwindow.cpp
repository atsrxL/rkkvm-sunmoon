// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include "capturearea.h"
#include "hid_keymap.h"
#include "serialbridge.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSerialPortInfo>
#include <QVBoxLayout>

using namespace rp2350;

namespace {
constexpr int kTypingStepMs = 30;
const char kTestText[] = "rkmoon";
constexpr qint64 kMaxSerialBacklog = 256;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , bridge_(new SerialBridge(this))
    , capture_(new CaptureArea)
{
    setWindowTitle(tr("RKMoon RP2350 HID 桥测试客户端"));

    portBox_ = new QComboBox;
    portBox_->setMinimumContentsLength(24);
    refreshButton_ = new QPushButton(tr("刷新"));
    baudBox_ = new QComboBox;
    baudBox_->setEditable(true);
    for (int b : {1000000, 921600, 460800, 230400, 115200})
        baudBox_->addItem(QString::number(b), b);
    baudBox_->setCurrentIndex(0);
    connectButton_ = new QPushButton(tr("连接"));

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("串口")));
    top->addWidget(portBox_, 1);
    top->addWidget(refreshButton_);
    top->addWidget(new QLabel(tr("波特率")));
    top->addWidget(baudBox_);
    top->addWidget(connectButton_);

    infoLabel_ = new QLabel(tr("—"));
    infoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pongLabel_ = new QLabel(tr("—"));
    linkLabel_ = new QLabel;
    auto *status = new QGroupBox(tr("板子状态"));
    auto *form = new QFormLayout(status);
    form->addRow(tr("链路"), linkLabel_);
    form->addRow(tr("INFO"), infoLabel_);
    form->addRow(tr("PONG"), pongLabel_);

    relButton_ = new QRadioButton(tr("相对鼠标"));
    absButton_ = new QRadioButton(tr("绝对鼠标"));
    relButton_->setChecked(true);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(relButton_);
    modeGroup->addButton(absButton_);
    releaseButton_ = new QPushButton(tr("释放全部"));
    testButton_ = new QPushButton(tr("测试：输入 \"rkmoon\""));
    auto *actions = new QHBoxLayout;
    actions->addWidget(relButton_);
    actions->addWidget(absButton_);
    actions->addStretch(1);
    actions->addWidget(testButton_);
    actions->addWidget(releaseButton_);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(500);
    log_->setMaximumHeight(120);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addLayout(top);
    layout->addWidget(status);
    layout->addLayout(actions);
    layout->addWidget(capture_, 1);
    layout->addWidget(log_);
    setCentralWidget(central);
    resize(900, 680);

    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(releaseButton_, &QPushButton::clicked, this, [this] {
        capture_->stopCapture(tr("按钮"));
        typingTimer_.stop();
        bridge_->sendReleaseAll();
        appendLog(tr("已发送 RELEASE_ALL"));
    });
    connect(testButton_, &QPushButton::clicked, this, &MainWindow::runTypingTest);
    connect(absButton_, &QRadioButton::toggled, capture_, &CaptureArea::setAbsoluteMode);

    connect(capture_, &CaptureArea::keyboardReport, bridge_, &SerialBridge::sendKeyboard);
    connect(capture_, &CaptureArea::mouseRelReport, bridge_, &SerialBridge::sendMouseRel);
    connect(capture_, &CaptureArea::mouseAbsReport, bridge_, &SerialBridge::sendMouseAbs);
    connect(capture_, &CaptureArea::releaseAllRequested, bridge_, &SerialBridge::sendReleaseAll);
    capture_->setMotionGate([this] { return bridge_->bytesPending() < kMaxSerialBacklog; });
    connect(capture_, &CaptureArea::captureChanged, this, [this](bool on, const QString &reason) {
        appendLog(on ? tr("开始捕获") : tr("释放捕获（%1），已发送 RELEASE_ALL").arg(reason));
        relButton_->setEnabled(!on);
        absButton_->setEnabled(!on);
    });

    connect(bridge_, &SerialBridge::infoReceived, this, [this](const InfoReply &info) {
        infoLabel_->setText(formatInfo(info));
        appendLog(tr("收到 INFO"));
        if (info.proto != kProtocolVersion)
            appendLog(tr("警告：板子协议版本 %1 与客户端 %2 不一致").arg(info.proto).arg(kProtocolVersion));
    });
    connect(bridge_, &SerialBridge::pongReceived, this, [this](const PongReply &pong, qint64 rtt) {
        pongLabel_->setText(formatPong(pong, rtt));
    });
    connect(bridge_, &SerialBridge::nakReceived, this, [this](const NakReply &nak) {
        appendLog(tr("NAK：type=0x%1 原因=%2 (%3)").arg(nak.origType, 2, 16, QLatin1Char('0')).arg(nak.reason).arg(QLatin1String(nakReasonText(nak.reason))));
    });
    connect(bridge_, &SerialBridge::linkChanged, this, [this](bool alive) {
        linkLabel_->setText(alive ? tr("<b style='color:#2a2'>在线</b>") : tr("<b style='color:#c22'>无应答</b>"));
        if (!alive && bridge_->isOpen())
            appendLog(tr("超过 1 秒没有收到板子应答"));
    });
    connect(bridge_, &SerialBridge::logMessage, this, &MainWindow::appendLog);
    connect(bridge_, &SerialBridge::closed, this, [this](const QString &reason) {
        appendLog(reason);
        setConnectedUi(false);
    });

    typingTimer_.setInterval(kTypingStepMs);
    connect(&typingTimer_, &QTimer::timeout, this, &MainWindow::typingStep);

    refreshPorts();
    setConnectedUi(false);
}

MainWindow::~MainWindow() = default;

QString MainWindow::formatInfo(const InfoReply &info)
{
    QStringList caps;
    if (info.caps & CapKeyboard) caps << QStringLiteral("键盘");
    if (info.caps & CapMouseRel) caps << QStringLiteral("相对鼠标");
    if (info.caps & CapMouseAbs) caps << QStringLiteral("绝对鼠标");
    if (info.caps & CapRemoteWakeup) caps << QStringLiteral("远程唤醒");
    const QByteArray serial(reinterpret_cast<const char *>(info.serial), 8);
    return QStringLiteral("协议 v%1，固件 %2.%3，能力 0x%4 [%5]，序列号 %6")
        .arg(info.proto)
        .arg(info.fwMajor)
        .arg(info.fwMinor)
        .arg(info.caps, 2, 16, QLatin1Char('0'))
        .arg(caps.join(QStringLiteral("、")), QString::fromLatin1(serial.toHex().toUpper()));
}

QString MainWindow::formatPong(const PongReply &pong, qint64 rttMs)
{
    auto yn = [](bool v) { return v ? QStringLiteral("是") : QStringLiteral("否"); };
    return QStringLiteral("USB 已配置：%1　被控机挂起：%2　有按键按下：%3　看门狗触发过：%4　接收错误：%5　丢弃：%6　往返：%7")
        .arg(yn(pong.status & StatusUsbConfigured), yn(pong.status & StatusHostSuspended),
             yn(pong.status & StatusInputHeld), yn(pong.status & StatusWatchdogFired))
        .arg(pong.rxErrors)
        .arg(pong.dropped)
        .arg(rttMs >= 0 ? QStringLiteral("%1 ms").arg(rttMs) : QStringLiteral("—"));
}

void MainWindow::refreshPorts()
{
    const QString current = portBox_->currentData().toString();
    portBox_->clear();
    for (const QSerialPortInfo &p : QSerialPortInfo::availablePorts()) {
        QString label = p.portName();
        if (!p.description().isEmpty())
            label += QStringLiteral(" — ") + p.description();
        if (p.hasVendorIdentifier())
            label += QStringLiteral(" [%1:%2]").arg(p.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(p.productIdentifier(), 4, 16, QLatin1Char('0'));
        portBox_->addItem(label, p.portName());
    }
    const int idx = portBox_->findData(current);
    if (idx >= 0)
        portBox_->setCurrentIndex(idx);
}

void MainWindow::toggleConnection()
{
    if (bridge_->isOpen()) {
        capture_->stopCapture(tr("断开连接"));
        typingTimer_.stop();
        bridge_->close();
        return;
    }
    const QString port = portBox_->currentData().toString();
    bool ok = false;
    const int baud = baudBox_->currentText().toInt(&ok);
    if (port.isEmpty() || !ok || baud <= 0) {
        appendLog(tr("请选择串口和有效波特率"));
        return;
    }
    infoLabel_->setText(tr("—"));
    pongLabel_->setText(tr("—"));
    QString error;
    if (!bridge_->open(port, baud, &error)) {
        appendLog(tr("打开 %1 失败：%2").arg(port, error));
        return;
    }
    appendLog(tr("已打开 %1 @ %2，发送 HELLO").arg(port).arg(baud));
    setConnectedUi(true);
}

void MainWindow::setConnectedUi(bool connected)
{
    connectButton_->setText(connected ? tr("断开") : tr("连接"));
    portBox_->setEnabled(!connected);
    baudBox_->setEnabled(!connected);
    refreshButton_->setEnabled(!connected);
    releaseButton_->setEnabled(connected);
    testButton_->setEnabled(connected);
    capture_->setEnabledForInput(connected);
    if (!connected)
        linkLabel_->setText(tr("未连接"));
}

void MainWindow::runTypingTest()
{
    if (!bridge_->isOpen() || typingTimer_.isActive())
        return;
    capture_->stopCapture(tr("文字测试"));
    typingQueue_ = QByteArray(kTestText);
    typingIndex_ = 0;
    typingKeyDown_ = false;
    appendLog(tr("开始向被控机输入测试文字（%1 个字符）").arg(typingQueue_.size()));
    typingTimer_.start();
}

void MainWindow::typingStep()
{
    const uint8_t none[6] = {};
    if (typingKeyDown_) {
        bridge_->sendKeyboard(0, QByteArray(reinterpret_cast<const char *>(none), 6));
        typingKeyDown_ = false;
        if (++typingIndex_ >= typingQueue_.size()) {
            typingTimer_.stop();
            appendLog(tr("测试文字发送完成"));
        }
        return;
    }
    uint8_t keys[6] = {hidUsageFromAscii(typingQueue_.at(typingIndex_))};
    bridge_->sendKeyboard(0, QByteArray(reinterpret_cast<const char *>(keys), 6));
    typingKeyDown_ = true;
}

void MainWindow::appendLog(const QString &message)
{
    log_->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz ")) + message);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    capture_->stopCapture(tr("关闭窗口"));
    typingTimer_.stop();
    bridge_->close();
    QMainWindow::closeEvent(e);
}

