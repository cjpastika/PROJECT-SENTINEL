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

/* ---- Configuration ---- */
#define SENSOR_SAMPLE_PERIOD_MS     20      /* 50 Hz */
#define SENSOR_TLM_PERIOD_MS        500     /* 2 Hz telemetry */
#define SENSOR_QUEUE_DEPTH          10

#define PRIORITY_SENSOR_SAMPLE      4       /* high — real-time sampling */
#define PRIORITY_SENSOR_TLM         2       /* low — telemetry output */

#define STACK_SIZE_SENSOR   ( configMINIMAL_STACK_SIZE * 3 )

/* ---- Global queue handle ---- */
QueueHandle_t g_imu_queue = NULL;

/* ---- Helper: integer to string (shared with main.c pattern) ---- */
static void int_to_str(int32_t val, char *buf, size_t buflen)
{
    char tmp[12];
    int i = 0;
    int neg = 0;

    if (val < 0) {
        neg = 1;
        val = -val;
    }

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0 && i < (int)sizeof(tmp)) {
            tmp[i++] = '0' + (char)(val % 10);
            val /= 10;
        }
    }

    int j = 0;
    if (neg && j < (int)buflen - 1) {
        buf[j++] = '-';
    }
    while (i > 0 && j < (int)buflen - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static void send_labeled_int(const char *label, int32_t val)
{
    char buf[12];
    hal_uart_send_string(label);
    int_to_str(val, buf, sizeof(buf));
    hal_uart_send_string(buf);
}

/* ---- Flight phase name for telemetry ---- */
static const char *get_phase_name(uint32_t tick_ms)
{
    if (tick_ms < 5000)       return "PAD";
    else if (tick_ms < 15000) return "BOOST";
    else if (tick_ms < 30000) return "COAST";
    else                      return "DESCENT";
}

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

    for (;;) {
        imu_reading_t reading;
        hal_sensor_read_imu(&reading);

        /* Non-blocking send — drop if queue is full */
        xQueueSend(g_imu_queue, &reading, 0);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_SAMPLE_PERIOD_MS));
    }
}

/* ================================================================
 * Sensor Telemetry Task — prints latest IMU data at 2 Hz
 *
 * Drains the queue to get the most recent sample, then prints
 * it as human-readable telemetry over UART.
 * ================================================================ */
static void task_sensor_tlm(void *params)
{
    (void)params;

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
            hal_uart_send_string("[IMU] t=");
            send_labeled_int("", (int32_t)latest.timestamp_ms);

            hal_uart_send_string(" phase=");
            hal_uart_send_string(get_phase_name(latest.timestamp_ms));

            send_labeled_int("  ax=", latest.accel.x);
            send_labeled_int(" ay=",  latest.accel.y);
            send_labeled_int(" az=",  latest.accel.z);
            send_labeled_int("  gx=", latest.gyro.x);
            send_labeled_int(" gy=",  latest.gyro.y);
            send_labeled_int(" gz=",  latest.gyro.z);
            hal_uart_send_string("\n");
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_TLM_PERIOD_MS));
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
