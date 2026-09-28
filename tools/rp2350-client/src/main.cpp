// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RKMoon RP2350 Client"));
    QApplication::setOrganizationName(QStringLiteral("RKMoon"));
    qRegisterMetaType<rp2350::InfoReply>();
    qRegisterMetaType<rp2350::PongReply>();
    qRegisterMetaType<rp2350::NakReply>();
    MainWindow w;
    w.show();
    return app.exec();
}

