/*
 * PROJECT-SENTINEL — Flight Computer Simulation
 *
 * Entry point: initializes hardware, creates RTOS tasks,
 * and starts the FreeRTOS scheduler.
 *
 * Phase 1: Minimal bringup with LED blink + UART hello world
 * Phase 2: Full flight computer task set + EKF (to be added)
 */

#include "FreeRTOS.h"
#include "task.h"

#include "hal_uart.h"
#include "hal_gpio.h"
#include "task_sensor.h"

/* ---- Task priorities (higher number = higher priority) ---- */
#define PRIORITY_LED_BLINK      1
#define PRIORITY_HEARTBEAT      2

/* ---- Task stack sizes (words) ---- */
#define STACK_SIZE_DEFAULT      ( configMINIMAL_STACK_SIZE * 2 )

/* ---- Forward declarations ---- */
static void task_led_blink(void *params);
static void task_heartbeat(void *params);

/* ---- Helper: simple integer-to-string ---- */
static void uint_to_str(uint32_t val, char *buf, size_t buflen)
{
    char tmp[12];
    int i = 0;

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0 && i < (int)sizeof(tmp)) {
            tmp[i++] = '0' + (val % 10);
            val /= 10;
        }
    }

    /* Reverse into output buffer */
    int j = 0;
    while (i > 0 && j < (int)buflen - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

/* ================================================================
 * main() — system entry point
 * ================================================================ */
int main(void)
{
    /* Initialize hardware abstraction */
    hal_uart_init();
    hal_gpio_init();

    /* Boot banner */
    hal_uart_send_string("\n");
    hal_uart_send_string("========================================\n");
    hal_uart_send_string("  PROJECT SENTINEL - Flight Computer\n");
    hal_uart_send_string("  Target: LM3S6965 / Cortex-M3 (QEMU)\n");
    hal_uart_send_string("  FreeRTOS Kernel v" tskKERNEL_VERSION_NUMBER "\n");
    hal_uart_send_string("========================================\n");
    hal_uart_send_string("[BOOT] Initializing RTOS tasks...\n");

    /* Create tasks */
    xTaskCreate(task_led_blink, "LED_BLINK", STACK_SIZE_DEFAULT,
                NULL, PRIORITY_LED_BLINK, NULL);

    xTaskCreate(task_heartbeat, "HEARTBEAT", STACK_SIZE_DEFAULT,
                NULL, PRIORITY_HEARTBEAT, NULL);

    /* Sensor subsystem: IMU sampling (50 Hz) + telemetry (2 Hz) */
    sensor_task_init();
    sensor_tlm_task_init();
    hal_uart_send_string("[BOOT] Sensor tasks created.\n");

    hal_uart_send_string("[BOOT] Starting scheduler.\n\n");

    /* Start FreeRTOS — should never return */
    vTaskStartScheduler();

    /* If we get here, heap was insufficient for idle task */
    hal_uart_send_string("[FATAL] Scheduler exited!\n");
    while (1) {}
}

/* ================================================================
 * LED Blink Task — toggles LED at 2 Hz (250 ms period)
 *
 * In a real flight computer this would be the watchdog-kicker /
 * aliveness indicator visible on the spacecraft bus.
 * ================================================================ */
static void task_led_blink(void *params)
{
    (void)params;

    for (;;) {
        hal_led_toggle();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* ================================================================
 * Heartbeat Task — prints a periodic status message over UART
 *
 * Demonstrates UART telemetry output that we can observe from the
 * host terminal. Prints a tick count so we can verify timing.
 * ================================================================ */
static void task_heartbeat(void *params)
{
    (void)params;
    uint32_t beat = 0;

    for (;;) {
        char num_buf[12];

        hal_uart_send_string("[TLM] Heartbeat #");
        uint_to_str(beat, num_buf, sizeof(num_buf));
        hal_uart_send_string(num_buf);

        hal_uart_send_string("  tick=");
        uint_to_str((uint32_t)xTaskGetTickCount(), num_buf, sizeof(num_buf));
        hal_uart_send_string(num_buf);

        hal_uart_send_string("  tasks=");
        uint_to_str((uint32_t)uxTaskGetNumberOfTasks(), num_buf, sizeof(num_buf));
        hal_uart_send_string(num_buf);

        hal_uart_send_string("\n");

        beat++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================================================================
 * FreeRTOS hook functions
 * ================================================================ */
void vApplicationMallocFailedHook(void)
{
    hal_uart_send_string("[FATAL] Malloc failed!\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    hal_uart_send_string("[FATAL] Stack overflow in task: ");
    hal_uart_send_string(pcTaskName);
    hal_uart_send_string("\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}
