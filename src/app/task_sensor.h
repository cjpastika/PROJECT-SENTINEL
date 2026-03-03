/*
 * Sensor task interface — IMU sampling and data distribution
 *
 * Creates a producer task that reads the IMU at a fixed rate and
 * pushes readings into a FreeRTOS queue. Other tasks can receive
 * from this queue to consume sensor data.
 */

#ifndef TASK_SENSOR_H
#define TASK_SENSOR_H

#include "FreeRTOS.h"
#include "queue.h"
#include "hal_sensor.h"

/* Queue handle for IMU readings — created by sensor_task_init() */
extern QueueHandle_t g_imu_queue;

/* Initialize sensor hardware and create the sensor sampling task */
void sensor_task_init(void);

/* Create a telemetry task that prints sensor data over UART */
void sensor_tlm_task_init(void);

#endif /* TASK_SENSOR_H */
