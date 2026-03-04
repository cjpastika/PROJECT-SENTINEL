/* Mock hal_sensor.h for host unit tests */
#ifndef HAL_SENSOR_H
#define HAL_SENSOR_H

#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
} vec3_t;

typedef struct {
    uint32_t timestamp_ms;
    vec3_t   accel;
    vec3_t   gyro;
} imu_reading_t;

static inline void hal_sensor_init(void) {}
static inline void hal_sensor_read_imu(imu_reading_t *r) { (void)r; }

#endif /* HAL_SENSOR_H */
