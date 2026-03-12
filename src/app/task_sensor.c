/*
 * Sensor task — IMU sampling and telemetry output
 *
 * task_sensor_sample:  Reads the IMU at 50 Hz and pushes each
 *                      reading into a FreeRTOS queue.
 *
 * task_sensor_tlm:     Consumes from the queue every 500 ms and
 *                      prints the latest reading over UART.
 *
 * The queue decouples the fast producer from slow consumers,
 * which is the standard pattern in flight software for sensor
 * data distribution.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "hal_uart.h"
#include "hal_sensor.h"
#include "task_sensor.h"
#include "tlm_frame.h"
#include "cmd_handler.h"
#include "task_watchdog.h"

/* ---- Configuration ---- */
#define SENSOR_SAMPLE_PERIOD_MS     20      /* 50 Hz */
#define SENSOR_TLM_PERIOD_MS        500     /* 2 Hz telemetry */
#define SENSOR_QUEUE_DEPTH          10

#define PRIORITY_SENSOR_SAMPLE      4       /* high — real-time sampling */
#define PRIORITY_SENSOR_TLM         2       /* low — telemetry output */

#define STACK_SIZE_SENSOR   ( configMINIMAL_STACK_SIZE * 3 )

/* ---- Global queue handle ---- */
QueueHandle_t g_imu_queue = NULL;

/* ================================================================
 * Sensor Sampling Task — runs at 50 Hz
 *
 * Reads the IMU and pushes each sample into the queue.
 * If the queue is full, the oldest sample is silently dropped
 * (overwrite-on-full is not used; we just skip the send).
 * ================================================================ */
static void task_sensor_sample(void *params)
{
    (void)params;
    TickType_t last_wake = xTaskGetTickCount();
    wdg_slot_t wdg = watchdog_register("IMU_SAMPLE", 100);

    for (;;) {
        imu_reading_t reading;
        hal_sensor_read_imu(&reading);

        /* Non-blocking send — drop if queue is full */
        xQueueSend(g_imu_queue, &reading, 0);

        watchdog_checkin(wdg);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_SAMPLE_PERIOD_MS));
    }
}

/* ================================================================
 * Sensor Telemetry Task — sends latest IMU data as binary packet
 *
 * Drains the queue to get the most recent sample, then sends it
 * as a framed TLM_MSG_IMU binary packet over UART.
 * ================================================================ */
static void task_sensor_tlm(void *params)
{
    (void)params;
    wdg_slot_t wdg = watchdog_register("IMU_TLM", 1200);

    for (;;) {
        imu_reading_t reading;
        imu_reading_t latest;
        BaseType_t got_any = pdFALSE;

        /* Drain queue to get the latest reading */
        while (xQueueReceive(g_imu_queue, &reading, 0) == pdTRUE) {
            latest = reading;
            got_any = pdTRUE;
        }

        if (got_any) {
            tlm_imu_t pkt;
            pkt.timestamp_ms = latest.timestamp_ms;
            pkt.accel_x      = latest.accel.x;
            pkt.accel_y      = latest.accel.y;
            pkt.accel_z      = latest.accel.z;
            pkt.gyro_x       = latest.gyro.x;
            pkt.gyro_y       = latest.gyro.y;
            pkt.gyro_z       = latest.gyro.z;

            tlm_send(TLM_MSG_IMU, &pkt, sizeof(pkt));
        }

        watchdog_checkin(wdg);
        vTaskDelay(pdMS_TO_TICKS(g_tlm_rate_ms));
    }
}

/* ================================================================
 * Public init functions
 * ================================================================ */
void sensor_task_init(void)
{
    hal_sensor_init();

    g_imu_queue = xQueueCreate(SENSOR_QUEUE_DEPTH, sizeof(imu_reading_t));
    configASSERT(g_imu_queue);

    xTaskCreate(task_sensor_sample, "IMU_SAMPLE", STACK_SIZE_SENSOR,
                NULL, PRIORITY_SENSOR_SAMPLE, NULL);
}

void sensor_tlm_task_init(void)
{
    xTaskCreate(task_sensor_tlm, "IMU_TLM", STACK_SIZE_SENSOR,
                NULL, PRIORITY_SENSOR_TLM, NULL);
}
