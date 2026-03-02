/*
 * HAL UART interface — hardware-agnostic UART API
 *
 * The flight software talks through this interface only;
 * the BSP/driver layer provides the actual implementation.
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>

void hal_uart_init(void);
void hal_uart_send_char(char c);
void hal_uart_send_string(const char *str);
void hal_uart_send_bytes(const uint8_t *data, size_t len);

#endif /* HAL_UART_H */
