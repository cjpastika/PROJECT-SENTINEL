/*
 * HAL GPIO interface — hardware-agnostic GPIO API
 *
 * On QEMU/lm3s6965evb the onboard LED is on GPIO port F, pin 0.
 * We abstract it here so application code doesn't touch registers.
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>

void hal_gpio_init(void);
void hal_led_toggle(void);
void hal_led_set(uint8_t on);

#endif /* HAL_GPIO_H */
