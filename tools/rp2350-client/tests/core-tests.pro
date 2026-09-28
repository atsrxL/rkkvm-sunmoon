# SPDX-License-Identifier: GPL-3.0-or-later
# Core-only test (protocol/keymap/state), buildable without QtSerialPort.
QT += core testlib
QT -= gui
CONFIG += testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = rp2350-core-tests
include(../core.pri)
SOURCES += tst_rp2350.cpp
DEFINES += RP2350_VECTORS_FILE=\\\"$$clean_path($$PWD/../../../docs/rp2350-protocol-vectors.json)\\\"

