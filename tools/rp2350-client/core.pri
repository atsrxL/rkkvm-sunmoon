# Shared protocol / keymap / input state sources (no GUI or serial dependency).
INCLUDEPATH += $$PWD/src
HEADERS += $$PWD/src/rp2350_protocol.h $$PWD/src/hid_keymap.h $$PWD/src/input_state.h
SOURCES += $$PWD/src/rp2350_protocol.cpp $$PWD/src/hid_keymap.cpp $$PWD/src/input_state.cpp
CONFIG += c++17
win32-msvc*: QMAKE_CXXFLAGS += /utf-8 /W4

