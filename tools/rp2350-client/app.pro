# SPDX-License-Identifier: GPL-3.0-or-later
QT += core gui widgets serialport
TEMPLATE = app
TARGET = rkmoon-rp2350-client
include(core.pri)
HEADERS += src/serialbridge.h src/capturearea.h src/mainwindow.h
SOURCES += src/serialbridge.cpp src/capturearea.cpp src/mainwindow.cpp src/main.cpp
win32: LIBS += user32.lib
win32: CONFIG += windows
