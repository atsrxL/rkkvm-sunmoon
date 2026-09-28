# Host unit tests for the hardware-independent core (no Pico SDK needed).
CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion
SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer
SRC := ../src/core/crc16.c ../src/core/proto.c ../src/core/bridge.c test_core.c test_vectors.c
VECTORS := ../../../docs/rp2350-protocol-vectors.json
OUT ?= ./build

.PHONY: test clean
test: $(OUT)/test_core
	$(OUT)/test_core

$(OUT)/vectors.h: $(VECTORS) gen_vectors.py
	mkdir -p $(OUT)
	python3 gen_vectors.py $(VECTORS) $@

$(OUT)/test_core: $(SRC) ../src/core/*.h $(OUT)/vectors.h
	$(CC) $(CFLAGS) $(SAN) -I$(OUT) -o $@ $(SRC)

clean:
	rm -rf $(OUT)
