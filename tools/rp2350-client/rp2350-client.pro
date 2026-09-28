# SPDX-License-Identifier: GPL-3.0-or-later
# RKMoon RP2350 HID bridge Windows test client (docs/ADR-012-rp2350-hid-bridge.md).
TEMPLATE = subdirs
SUBDIRS = app tests
app.file = app.pro
tests.file = tests/tests.pro

