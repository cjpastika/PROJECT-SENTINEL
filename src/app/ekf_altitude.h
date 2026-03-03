/*
 * 1D Altitude Extended Kalman Filter for PROJECT-SENTINEL
 *
 * State vector: x = [altitude (m), velocity (m/s)]
 * Input:        accelerometer Z-axis (milli-g)
 * Output:       estimated altitude and velocity
 *
 * The EKF fuses IMU accelerometer data to estimate vertical
 * position and velocity. This is a simplified 1D estimator
 * suitable for a sounding rocket profile.
 *
 * Coordinate convention:
 *   +Z = up (away from ground)
 *   Accel reading on pad = +1000 mg (gravity pointing up through sensor)
 *   Gravity compensation: subtract 1g before integrating
 */

#ifndef EKF_ALTITUDE_H
#define EKF_ALTITUDE_H

#include <stdint.h>

/* ---- EKF state structure ---- */
typedef struct {
    /* State estimate: [altitude_m, velocity_ms] */
    float x[2];

    /* Error covariance (2x2 symmetric, stored as [P00, P01, P10, P11]) */
    float P[4];

    /* Last update timestamp (ms) */
    uint32_t last_tick_ms;

    /* Initialized flag */
    uint8_t initialized;
} ekf_state_t;

/* ---- Telemetry message ID ---- */
#define TLM_MSG_EKF         0x07

/* ---- EKF telemetry payload ---- */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    int32_t  altitude_mm;       /* altitude in millimeters */
    int32_t  velocity_mms;      /* velocity in mm/s */
    int32_t  accel_input_mg;    /* raw accel Z input (milli-g) */
} tlm_ekf_t;

/*
 * Initialize the EKF state.
 * Call once before the first predict/update cycle.
 */
void ekf_init(ekf_state_t *ekf);

/*
 * Predict + update step.
 *   accel_z_mg:  Z-axis accelerometer reading in milli-g
 *   tick_ms:     current FreeRTOS tick count
 *
 * Performs time-propagation using the acceleration input,
 * then updates the covariance.
 */
void ekf_update(ekf_state_t *ekf, int32_t accel_z_mg, uint32_t tick_ms);

/*
 * Initialize and start the EKF task.
 * The task reads from the IMU queue and runs the filter at 50 Hz.
 */
void ekf_task_init(void);

#endif /* EKF_ALTITUDE_H */
