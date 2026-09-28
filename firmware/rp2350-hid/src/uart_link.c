// SPDX-License-Identifier: GPL-3.0-or-later
#include "uart_link.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/sync.h"

#define LINK_UART uart0
#define RX_RING 2048u // ~20 ms at 1 Mbaud; power of two
#define TX_RING 512u

static uint8_t rx_buf[RX_RING];
static volatile uint32_t rx_head; // written by IRQ
static volatile uint32_t rx_tail; // written by main loop
static volatile uint32_t rx_line_errors;
static volatile uint32_t rx_dropped;

static uint8_t tx_buf[TX_RING];
static uint32_t tx_head, tx_tail;

static void on_uart_rx(void) {
    uart_hw_t *hw = uart_get_hw(LINK_UART);
    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
        uint32_t dr = hw->dr;
        if (dr & UART_UARTDR_OE_BITS) {
            rx_dropped++; // hardware FIFO overrun: at least one byte lost
        }
        if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_PE_BITS | UART_UARTDR_BE_BITS)) {
            rx_line_errors++;
            continue;
        }
        uint32_t h = rx_head;
        if (h - rx_tail >= RX_RING) {
            rx_dropped++;
            continue;
        }
        rx_buf[h & (RX_RING - 1)] = (uint8_t)dr;
        __dmb();
        rx_head = h + 1;
    }
}

void uart_link_init(void) {
    uart_init(LINK_UART, RKMOON_UART_BAUD);
    uart_set_format(LINK_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(LINK_UART, false, false);
    uart_set_fifo_enabled(LINK_UART, true);
    gpio_set_function(RKMOON_UART_TX_PIN, UART_FUNCSEL_NUM(LINK_UART, RKMOON_UART_TX_PIN));
    gpio_set_function(RKMOON_UART_RX_PIN, UART_FUNCSEL_NUM(LINK_UART, RKMOON_UART_RX_PIN));
    gpio_pull_up(RKMOON_UART_RX_PIN); // idle-high when the adapter is unplugged
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irqs_enabled(LINK_UART, true, false); // RX + RX timeout
}

bool uart_link_peek(uint8_t *byte) {
    uint32_t t = rx_tail;
    if (t == rx_head) {
        return false;
    }
    __dmb();
    *byte = rx_buf[t & (RX_RING - 1)];
    return true;
}

void uart_link_pop(void) {
    if (rx_tail != rx_head) {
        rx_tail = rx_tail + 1;
    }
}

void uart_link_take_errors(uint32_t *line_errors, uint32_t *dropped) {
    uint32_t s = save_and_disable_interrupts();
    *line_errors = rx_line_errors;
    *dropped = rx_dropped;
    rx_line_errors = 0;
    rx_dropped = 0;
    restore_interrupts(s);
}

void uart_link_write(const uint8_t *data, size_t len) {
    if ((size_t)(TX_RING - (tx_head - tx_tail)) < len) {
        return; // replies are advisory; never block the input path
    }
    for (size_t i = 0; i < len; i++) {
        tx_buf[(tx_head++) & (TX_RING - 1)] = data[i];
    }
    uart_link_poll_tx();
}

void uart_link_poll_tx(void) {
    while (tx_tail != tx_head && uart_is_writable(LINK_UART)) {
        uart_get_hw(LINK_UART)->dr = tx_buf[(tx_tail++) & (TX_RING - 1)];
    }
}
