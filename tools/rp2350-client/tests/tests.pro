# SPDX-License-Identifier: GPL-3.0-or-later
QT += core gui widgets serialport testlib
CONFIG += testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = rp2350-client-tests
include(../core.pri)
INCLUDEPATH += ../src
HEADERS += ../src/serialbridge.h ../src/capturearea.h ../src/mainwindow.h
SOURCES += ../src/serialbridge.cpp ../src/capturearea.cpp ../src/mainwindow.cpp tst_rp2350.cpp
win32: LIBS += user32.lib
DEFINES += RP2350_WITH_GUI
DEFINES += RP2350_VECTORS_FILE=\\\"$$clean_path($$PWD/../../../docs/rp2350-protocol-vectors.json)\\\"
