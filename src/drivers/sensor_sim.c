/*
 * Simulated IMU driver for PROJECT-SENTINEL
 *
 * Generates synthetic 6-axis IMU data (accelerometer + gyroscope)
 * that mimics a rocket flight profile:
 *
 *   Phase 0 (PAD):    sitting on pad, accel ~= (0, 0, +1000 mg), gyro ~= 0
 *   Phase 1 (BOOST):  high Z accel (thrust), slight rotation
 *   Phase 2 (COAST):  near-zero accel (microgravity), slow tumble
 *   Phase 3 (DESCENT): negative Z accel (drag), increasing rotation
 *   Phase 4 (LANDED):  back on ground, accel ~= (0, 0, +1000 mg), gyro ~= 0
 *
 * Uses a simple LCG PRNG to add sensor noise without needing <stdlib.h>.
 * All values are in milli-g (accel) and milli-deg/s (gyro).
 */

#include "hal_sensor.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- Simple LCG pseudo-random number generator ---- */
static uint32_t prng_state = 12345;

static int32_t prng_range(int32_t min, int32_t max)
{
    prng_state = prng_state * 1103515245U + 12345U;
    uint32_t raw = (prng_state >> 16) & 0x7FFF;
    if (max <= min) return min;
    return min + (int32_t)(raw % (uint32_t)(max - min + 1));
}

/* ---- Flight phase timing (in ms from boot) ---- */
#define PHASE_PAD_END       5000    /* 0-5s:   on the pad */
#define PHASE_BOOST_END     15000   /* 5-15s:  powered ascent */
#define PHASE_COAST_END     30000   /* 15-30s: coasting */
#define PHASE_DESCENT_END   45000   /* 30-45s: descent / landing */
                                    /* 45s+:   on the ground */

void hal_sensor_init(void)
{
    /* Seed the PRNG with something varying */
    prng_state = 42;
}

void hal_sensor_read_imu(imu_reading_t *reading)
{
    uint32_t now = (uint32_t)xTaskGetTickCount();
    reading->timestamp_ms = now;

    if (now < PHASE_PAD_END) {
        /* PAD: gravity pointing up through Z axis */
        reading->accel.x = prng_range(-15, 15);
        reading->accel.y = prng_range(-15, 15);
        reading->accel.z = 1000 + prng_range(-10, 10);

        reading->gyro.x = prng_range(-50, 50);
        reading->gyro.y = prng_range(-50, 50);
        reading->gyro.z = prng_range(-30, 30);

    } else if (now < PHASE_BOOST_END) {
        /* BOOST: strong Z acceleration (thrust + gravity) */
        uint32_t elapsed = now - PHASE_PAD_END;
        int32_t thrust = 3000 + (int32_t)(elapsed / 5);
        if (thrust > 6000) thrust = 6000;

        reading->accel.x = prng_range(-100, 100);
        reading->accel.y = prng_range(-100, 100);
        reading->accel.z = thrust + prng_range(-50, 50);

        reading->gyro.x = prng_range(-200, 200);
        reading->gyro.y = prng_range(-200, 200);
        reading->gyro.z = 500 + prng_range(-100, 100);

    } else if (now < PHASE_COAST_END) {
        /* COAST: microgravity, slow tumble */
        reading->accel.x = prng_range(-30, 30);
        reading->accel.y = prng_range(-30, 30);
        reading->accel.z = prng_range(-30, 30);

        reading->gyro.x = 300 + prng_range(-50, 50);
        reading->gyro.y = -200 + prng_range(-50, 50);
        reading->gyro.z = 100 + prng_range(-50, 50);

    } else if (now < PHASE_DESCENT_END) {
        /* DESCENT: drag deceleration, increasing rotation */
        uint32_t elapsed = now - PHASE_COAST_END;
        int32_t drag = -500 - (int32_t)(elapsed / 10);
        if (drag < -2000) drag = -2000;

        reading->accel.x = prng_range(-150, 150);
        reading->accel.y = prng_range(-150, 150);
        reading->accel.z = drag + prng_range(-80, 80);

        reading->gyro.x = 1000 + prng_range(-300, 300);
        reading->gyro.y = -800 + prng_range(-300, 300);
        reading->gyro.z = 500 + prng_range(-200, 200);

    } else {
        /* LANDED: back on ground, gravity only */
        reading->accel.x = prng_range(-15, 15);
        reading->accel.y = prng_range(-15, 15);
        reading->accel.z = 1000 + prng_range(-10, 10);

        reading->gyro.x = prng_range(-50, 50);
        reading->gyro.y = prng_range(-50, 50);
        reading->gyro.z = prng_range(-30, 30);
    }
}
