/* Mock task_sensor.h for host unit tests */
#ifndef TASK_SENSOR_H
#define TASK_SENSOR_H

#include "mock_freertos.h"
#include "hal_sensor.h"

static QueueHandle_t g_imu_queue = NULL;

static inline void sensor_task_init(void) {}
static inline void sensor_tlm_task_init(void) {}

#endif /* TASK_SENSOR_H */
