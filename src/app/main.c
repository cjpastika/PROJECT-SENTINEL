/*
 * PROJECT-SENTINEL — Flight Computer Simulation
 *
 * Entry point: initializes hardware, creates RTOS tasks,
 * and starts the FreeRTOS scheduler.
 *
 * Phase 1: Minimal bringup with LED blink + UART hello world
 * Phase 2: Full flight computer task set + EKF
 */

#include "FreeRTOS.h"
#include "task.h"

#include "hal_uart.h"
#include "hal_gpio.h"
#include "task_sensor.h"
#include "tlm_frame.h"
#include "cmd_handler.h"
#include "task_watchdog.h"
#include "flight_sm.h"
#include "ekf_altitude.h"
#include "datalog.h"
#include "fault_mgr.h"
#include "tmr.h"

/* ---- Task priorities (higher number = higher priority) ---- */
#define PRIORITY_LED_BLINK      1
#define PRIORITY_HEARTBEAT      2

/* ---- Task stack sizes (words) ---- */
#define STACK_SIZE_DEFAULT      ( configMINIMAL_STACK_SIZE * 2 )

/* ---- Forward declarations ---- */
static void task_led_blink(void *params);
static void task_heartbeat(void *params);


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

    /* Fault manager: centralized anomaly recording */
    fault_mgr_init();
    hal_uart_send_string("[BOOT] Fault manager initialized.\n");

    /* Data logger: simulated flash circular buffer */
    datalog_init();
    hal_uart_send_string("[BOOT] Flash data logger initialized (4 KB).\n");

    fault_record(FAULT_SEV_INFO, FAULT_SRC_SYSTEM,
                 FAULT_DETAIL_BOOT, 0);

    /* Sensor subsystem: IMU sampling (50 Hz) + telemetry (2 Hz) */
    sensor_task_init();
    sensor_tlm_task_init();
    hal_uart_send_string("[BOOT] Sensor tasks created.\n");

    /* Command handler: UART RX parser + dispatcher */
    cmd_handler_init();
    hal_uart_send_string("[BOOT] Command handler ready (press '?' for help).\n");

    /* Flight state machine: mission mode manager */
    flight_sm_init();
    hal_uart_send_string("[BOOT] Flight state machine ready.\n");

    /* EKF: 1D altitude estimator (50 Hz, fuses accel Z) */
    ekf_task_init();
    hal_uart_send_string("[BOOT] EKF altitude estimator started.\n");

    /* TMR: Triple Modular Redundancy voter over 3 EKF channels */
    tmr_ekf_init();
    hal_uart_send_string("[BOOT] TMR voting (3-channel EKF) started.\n");

    /* Task watchdog: software timer monitors task deadlines */
    watchdog_init();
    hal_uart_send_string("[BOOT] Task watchdog started.\n");

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
    wdg_slot_t wdg = watchdog_register("LED_BLINK", 600);

    for (;;) {
        hal_led_toggle();
        watchdog_checkin(wdg);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* ================================================================
 * Heartbeat Task — sends periodic heartbeat telemetry packet
 *
 * Sends a binary-framed TLM_MSG_HEARTBEAT packet containing the
 * beat counter, tick count, and active task count.
 * ================================================================ */
static void task_heartbeat(void *params)
{
    (void)params;
    uint32_t beat = 0;
    wdg_slot_t wdg = watchdog_register("HEARTBEAT", 2000);

    for (;;) {
        tlm_heartbeat_t hb;
        hb.beat_count = beat;
        hb.tick_ms    = (uint32_t)xTaskGetTickCount();
        hb.task_count = (uint8_t)uxTaskGetNumberOfTasks();
        hb.pad[0] = 0;
        hb.pad[1] = 0;
        hb.pad[2] = 0;

        tlm_send_debug(TLM_MSG_HEARTBEAT, &hb, sizeof(hb));

        watchdog_checkin(wdg);
        beat++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================================================================
 * FreeRTOS hook functions
 * ================================================================ */
void vApplicationMallocFailedHook(void)
{
    fault_record(FAULT_SEV_CRITICAL, FAULT_SRC_RTOS,
                 FAULT_DETAIL_MALLOC_FAIL, 0);
    hal_uart_send_string("[FATAL] Malloc failed!\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    fault_record(FAULT_SEV_CRITICAL, FAULT_SRC_RTOS,
                 FAULT_DETAIL_STACK_OVERFLOW, 0);
    hal_uart_send_string("[FATAL] Stack overflow in task: ");
    hal_uart_send_string(pcTaskName);
    hal_uart_send_string("\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}
