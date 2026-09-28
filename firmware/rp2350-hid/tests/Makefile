# Host unit tests for the hardware-independent core (no Pico SDK needed).
CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion
SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer
SRC := ../src/core/crc16.c ../src/core/proto.c ../src/core/bridge.c test_core.c test_vectors.c
VECTORS := ../../../docs/rp2350-protocol-vectors.json
OUT ?= ./build

.PHONY: test clean
test: $(OUT)/test_core $(OUT)/test_cdc $(OUT)/test_core_off $(OUT)/test_cdc_transport
	$(OUT)/test_core
	$(OUT)/test_core_off
	$(OUT)/test_cdc
	$(OUT)/test_cdc_transport

$(OUT)/vectors.h: $(VECTORS) gen_vectors.py
	mkdir -p $(OUT)
	python3 gen_vectors.py $(VECTORS) $@

$(OUT)/test_core: $(SRC) ../src/core/*.h $(OUT)/vectors.h
	$(CC) $(CFLAGS) $(SAN) -I$(OUT) -o $@ $(SRC)

clean:
	python3 -c 'import shutil; shutil.rmtree("$(OUT)", ignore_errors=True)'

$(OUT)/test_core_off: $(SRC) ../src/core/*.h $(OUT)/vectors.h
	$(CC) $(CFLAGS) $(SAN) -DRKMOON_PIO_USB_CDC=0 -I$(OUT) -o $@ $(SRC)

$(OUT)/test_cdc: test_cdc.c ../src/core/cdc_control.c ../src/core/*.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SAN) -pthread -o $@ test_cdc.c ../src/core/cdc_control.c ../src/core/bridge.c ../src/core/proto.c ../src/core/crc16.c

$(OUT)/test_cdc_transport: test_cdc_transport.c cdc_transport_stub.h ../src/pio_cdc.c ../src/core/*.c ../src/core/*.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SAN) -Wno-unused-parameter -I. -I../src/core -o $@ test_cdc_transport.c ../src/core/cdc_control.c ../src/core/bridge.c ../src/core/proto.c ../src/core/crc16.c
