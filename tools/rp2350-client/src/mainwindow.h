// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rp2350_protocol.h"

#include <QMainWindow>
#include <QTimer>

class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QPlainTextEdit;
class CaptureArea;
class SerialBridge;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Exposed for tests.
    SerialBridge *bridge() const { return bridge_; }
    CaptureArea *captureArea() const { return capture_; }
    static QString formatInfo(const rp2350::InfoReply &info);
    static QString formatPong(const rp2350::PongReply &pong, qint64 rttMs);

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void refreshPorts();
    void toggleConnection();
    void runTypingTest();
    void typingStep();
    void appendLog(const QString &message);

private:
    void setConnectedUi(bool connected);

    SerialBridge *bridge_;
    CaptureArea *capture_;
    QComboBox *portBox_;
    QComboBox *baudBox_;
    QPushButton *refreshButton_;
    QPushButton *connectButton_;
    QPushButton *releaseButton_;
    QPushButton *testButton_;
    QRadioButton *relButton_;
    QRadioButton *absButton_;
    QLabel *infoLabel_;
    QLabel *pongLabel_;
    QLabel *linkLabel_;
    QPlainTextEdit *log_;
    QTimer typingTimer_;
    QByteArray typingQueue_;
    int typingIndex_ = 0;
    bool typingKeyDown_ = false;
};

