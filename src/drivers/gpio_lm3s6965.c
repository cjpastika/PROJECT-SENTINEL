/*
 * GPIO driver for LM3S6965 — LED control
 *
 * The LM3S6965 eval board has a user LED on GPIO Port F, pin 0.
 * QEMU does not visually render the LED, but we toggle the register
 * so the logic is correct and telemetry can report the state.
 */

#include "hal_gpio.h"

/* LM3S6965 GPIO Port F registers */
#define GPIOF_BASE   0x40025000UL
#define GPIOF_DATA   (*(volatile uint32_t *)(GPIOF_BASE + 0x3FC))
#define GPIOF_DIR    (*(volatile uint32_t *)(GPIOF_BASE + 0x400))
#define GPIOF_DEN    (*(volatile uint32_t *)(GPIOF_BASE + 0x51C))

/* System control — GPIO clock gating */
#define SYSCTL_BASE  0x400FE000UL
#define SYSCTL_RCGC2 (*(volatile uint32_t *)(SYSCTL_BASE + 0x108))

#define LED_PIN  (1U << 0)

static uint8_t led_state = 0;

void hal_gpio_init(void)
{
    /* Enable clock to GPIO Port F */
    SYSCTL_RCGC2 |= (1U << 5);

    /* Small delay for clock to stabilize (volatile read) */
    volatile uint32_t delay = SYSCTL_RCGC2;
    (void)delay;

    /* Set PF0 as output, enable digital function */
    GPIOF_DIR |= LED_PIN;
    GPIOF_DEN |= LED_PIN;

    /* Start with LED off */
    GPIOF_DATA &= ~LED_PIN;
    led_state = 0;
}

void hal_led_toggle(void)
{
    led_state ^= 1;
    if (led_state) {
        GPIOF_DATA |= LED_PIN;
    } else {
        GPIOF_DATA &= ~LED_PIN;
    }
}

void hal_led_set(uint8_t on)
{
    led_state = on ? 1 : 0;
    if (led_state) {
        GPIOF_DATA |= LED_PIN;
    } else {
        GPIOF_DATA &= ~LED_PIN;
    }
}
