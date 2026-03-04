/*
 * Mock HAL layer for host-compiled unit tests.
 * UART/GPIO/sensor calls become no-ops or simple stubs.
 */

#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <stdint.h>
#include <stddef.h>

/* ---- UART stubs ---- */
static inline void hal_uart_init(void) {}
static inline void hal_uart_send_char(char c) { (void)c; }
static inline void hal_uart_send_string(const char *s) { (void)s; }
static inline void hal_uart_send_bytes(const uint8_t *d, size_t l) { (void)d; (void)l; }
static inline int  hal_uart_rx_ready(void) { return 0; }
static inline char hal_uart_recv_char(void) { return 0; }

/* ---- GPIO stubs ---- */
static inline void hal_gpio_init(void) {}
static inline void hal_led_toggle(void) {}
static inline void hal_led_set(int s) { (void)s; }

#endif /* MOCK_HAL_H */
