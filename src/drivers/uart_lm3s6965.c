/*
 * UART0 driver for LM3S6965 (Stellaris/Tiva) on QEMU
 *
 * QEMU's lm3s6965evb emulates UART0 at 0x4000C000.
 * Characters written to the data register appear on the host terminal.
 * We don't need baud-rate setup on QEMU — the emulation handles it.
 */

#include "hal_uart.h"

/* LM3S6965 UART0 register base */
#define UART0_BASE  0x4000C000UL

/* Register offsets */
#define UART_DR     (*(volatile uint32_t *)(UART0_BASE + 0x000)) /* Data */
#define UART_FR     (*(volatile uint32_t *)(UART0_BASE + 0x018)) /* Flag */

/* Flag register bits */
#define UART_FR_RXFE  (1U << 4)  /* Receive FIFO empty */
#define UART_FR_TXFF  (1U << 5)  /* Transmit FIFO full */

void hal_uart_init(void)
{
    /* QEMU UART0 works out of the box — no clock/pin setup needed */
}

void hal_uart_send_char(char c)
{
    /* Wait until TX FIFO is not full */
    while (UART_FR & UART_FR_TXFF) {}
    UART_DR = (uint32_t)c;
}

void hal_uart_send_string(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            hal_uart_send_char('\r');
        }
        hal_uart_send_char(*str++);
    }
}

void hal_uart_send_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        hal_uart_send_char((char)data[i]);
    }
}

int hal_uart_rx_ready(void)
{
    return (UART_FR & UART_FR_RXFE) ? 0 : 1;
}

char hal_uart_recv_char(void)
{
    while (UART_FR & UART_FR_RXFE) {}
    return (char)(UART_DR & 0xFF);
}
