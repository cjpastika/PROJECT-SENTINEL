/*
 * HAL Sensor interface — hardware-agnostic IMU API
 *
 * Defines the data structures and functions for a 6-axis IMU
 * (3-axis accelerometer + 3-axis gyroscope). The driver layer
 * provides the actual implementation (real hardware or simulation).
 */

#ifndef HAL_SENSOR_H
#define HAL_SENSOR_H

#include <stdint.h>

/* 3-axis vector (used for both accel and gyro) */
typedef struct {
    int32_t x;  /* milli-g (accel) or milli-deg/s (gyro) */
    int32_t y;
    int32_t z;
} vec3_t;

/* Complete IMU reading with timestamp */
typedef struct {
    uint32_t timestamp_ms;  /* tick count at time of reading */
    vec3_t   accel;         /* accelerometer in milli-g */
    vec3_t   gyro;          /* gyroscope in milli-deg/s */
} imu_reading_t;

/* Initialize the sensor hardware / simulation */
void hal_sensor_init(void);

/* Read current IMU sample (fills out the reading struct) */
void hal_sensor_read_imu(imu_reading_t *reading);

#endif /* HAL_SENSOR_H */
