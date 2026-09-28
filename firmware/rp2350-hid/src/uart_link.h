// SPDX-License-Identifier: GPL-3.0-or-later
// UART0 control channel: IRQ-driven RX ring, non-blocking TX ring.
#ifndef RKMOON_UART_LINK_H
#define RKMOON_UART_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void uart_link_init(void);
bool uart_link_peek(uint8_t *byte); // next RX byte without consuming it
void uart_link_pop(void);
// Errors/overflows since the last call (framing/parity/break, lost bytes).
void uart_link_take_errors(uint32_t *line_errors, uint32_t *dropped);
void uart_link_write(const uint8_t *data, size_t len); // drops if TX ring full
void uart_link_poll_tx(void);

#endif
