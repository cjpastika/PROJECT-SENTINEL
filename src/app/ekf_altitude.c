/*
 * 1D Altitude EKF — Kalman filter for vertical state estimation
 *
 * State:   x = [altitude (m), velocity (m/s)]
 * Input:   u = accelerometer Z (m/s^2), gravity-compensated
 * Model:   x_new = F * x + B * u
 *          F = [1, dt; 0, 1]
 *          B = [0.5*dt^2; dt]
 *
 * This is a linear KF (not extended) since the state model is
 * linear. The "EKF" name is used because the framework supports
 * future extension to nonlinear models (e.g., barometric altitude).
 *
 * Process noise Q models accelerometer bias and vibration.
 * No measurement update is performed (pure propagation) since
 * we have no independent altitude sensor — the filter serves
 * as an integrator with covariance tracking.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "hal_uart.h"
#include "hal_sensor.h"
#include "ekf_altitude.h"
#include "task_sensor.h"
#include "tlm_frame.h"
#include "task_watchdog.h"
#include "flight_sm.h"

/* ---- Configuration ---- */
#define EKF_PERIOD_MS       20      /* 50 Hz, matches IMU sample rate */
#define PRIORITY_EKF        4       /* same as sensor sampling */
#define STACK_SIZE_EKF      ( configMINIMAL_STACK_SIZE * 4 )

/* ---- Physical constants ---- */
#define GRAVITY_MG          1000    /* 1g in milli-g */
#define MG_TO_MS2           0.00981f /* milli-g to m/s^2 */

/* ---- Process noise tuning ---- */
#define Q_ACCEL_NOISE       2.0f    /* m/s^2 — accelerometer noise std dev */

/* ---- Telemetry decimation ---- */
#define TLM_DECIMATION      25      /* send telemetry every 25th cycle (2 Hz) */

/* ================================================================
 * EKF Core
 * ================================================================ */

void ekf_init(ekf_state_t *ekf)
{
    ekf->x[0] = 0.0f;  /* altitude = 0 m */
    ekf->x[1] = 0.0f;  /* velocity = 0 m/s */

    /* Initial covariance — moderate uncertainty */
    ekf->P[0] = 1.0f;   /* P00: altitude variance */
    ekf->P[1] = 0.0f;   /* P01 */
    ekf->P[2] = 0.0f;   /* P10 */
    ekf->P[3] = 1.0f;   /* P11: velocity variance */

    ekf->last_tick_ms = 0;
    ekf->initialized = 0;
}

void ekf_update(ekf_state_t *ekf, int32_t accel_z_mg, uint32_t tick_ms)
{
    if (!ekf->initialized) {
        ekf->last_tick_ms = tick_ms;
        ekf->initialized = 1;
        return;
    }

    /* Compute dt in seconds */
    uint32_t dt_ms = tick_ms - ekf->last_tick_ms;
    if (dt_ms == 0 || dt_ms > 1000) {
        /* Skip if no time elapsed or too large a gap */
        ekf->last_tick_ms = tick_ms;
        return;
    }
    float dt = (float)dt_ms / 1000.0f;
    ekf->last_tick_ms = tick_ms;

    /* Gravity-compensate: subtract 1g (sensor reads +1g on pad) */
    float accel_ms2 = (float)(accel_z_mg - GRAVITY_MG) * MG_TO_MS2;

    /* ---- Predict step ---- */
    /* State propagation: x = F*x + B*u */
    float dt2_half = 0.5f * dt * dt;
    float alt_new = ekf->x[0] + ekf->x[1] * dt + dt2_half * accel_ms2;
    float vel_new = ekf->x[1] + dt * accel_ms2;

    ekf->x[0] = alt_new;
    ekf->x[1] = vel_new;

    /* Covariance propagation: P = F*P*F' + Q */
    float P00 = ekf->P[0];
    float P01 = ekf->P[1];
    float P10 = ekf->P[2];
    float P11 = ekf->P[3];

    /* F*P */
    float FP00 = P00 + dt * P10;
    float FP01 = P01 + dt * P11;
    float FP10 = P10;
    float FP11 = P11;

    /* F*P*F' */
    ekf->P[0] = FP00 + dt * FP01;
    ekf->P[1] = FP01;
    ekf->P[2] = FP10 + dt * FP11;
    ekf->P[3] = FP11;

    /* Add process noise Q */
    float q = Q_ACCEL_NOISE * Q_ACCEL_NOISE;
    ekf->P[0] += q * dt2_half * dt2_half;      /* dt^4/4 * q */
    ekf->P[1] += q * dt2_half * dt;             /* dt^3/2 * q */
    ekf->P[2] += q * dt * dt2_half;             /* dt^3/2 * q */
    ekf->P[3] += q * dt * dt;                   /* dt^2 * q */

    /* Clamp altitude to non-negative during pad/landed phases */
    flight_state_t state = flight_sm_get_state();
    if (state == FLIGHT_IDLE || state == FLIGHT_ARMED || state == FLIGHT_LANDED) {
        if (ekf->x[0] < 0.0f) ekf->x[0] = 0.0f;
        if (state == FLIGHT_LANDED) ekf->x[1] = 0.0f;
    }

    /* Once altitude hits ground during descent, zero both alt and velocity */
    if (state == FLIGHT_DESCENT && ekf->x[0] <= 0.0f) {
        ekf->x[0] = 0.0f;
        ekf->x[1] = 0.0f;
    }
}

/* ================================================================
 * EKF Task — runs at 50 Hz, reads IMU queue, propagates filter
 * ================================================================ */
static ekf_state_t g_ekf;

static void task_ekf(void *params)
{
    (void)params;

    ekf_init(&g_ekf);
    wdg_slot_t wdg = watchdog_register("EKF", 100);
    uint32_t tlm_counter = 0;
    int32_t last_accel = 0;

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        imu_reading_t reading;

        if (xQueuePeek(g_imu_queue, &reading, 0) == pdTRUE) {
            last_accel = reading.accel.z;
            ekf_update(&g_ekf, reading.accel.z, reading.timestamp_ms);
        }

        /* Send telemetry at decimated rate */
        tlm_counter++;
        if (tlm_counter >= TLM_DECIMATION && g_ekf.initialized) {
            tlm_counter = 0;

            tlm_ekf_t pkt;
            pkt.timestamp_ms   = (uint32_t)xTaskGetTickCount();
            pkt.altitude_mm    = (int32_t)(g_ekf.x[0] * 1000.0f);
            pkt.velocity_mms   = (int32_t)(g_ekf.x[1] * 1000.0f);
            pkt.accel_input_mg = last_accel;

            tlm_send_debug(TLM_MSG_EKF, &pkt, sizeof(pkt));
        }

        watchdog_checkin(wdg);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(EKF_PERIOD_MS));
    }
}

/* ================================================================
 * Public init
 * ================================================================ */
void ekf_task_init(void)
{
    xTaskCreate(task_ekf, "EKF", STACK_SIZE_EKF,
                NULL, PRIORITY_EKF, NULL);
}
