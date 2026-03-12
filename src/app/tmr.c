/*
 * Triple Modular Redundancy — 3-channel EKF voter
 *
 * Runs three independent EKF altitude estimators with slightly
 * different process noise parameters (simulating manufacturing
 * variation in real redundant hardware). The median of the three
 * altitude and velocity estimates is selected as the voted output.
 *
 * Disagreements beyond a threshold are reported to the fault
 * manager, and per-channel health is tracked in telemetry.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "hal_uart.h"
#include "tmr.h"
#include "ekf_altitude.h"
#include "task_sensor.h"
#include "tlm_frame.h"
#include "task_watchdog.h"
#include "flight_sm.h"
#include "fault_mgr.h"

/* ---- Configuration ---- */
#define TMR_PERIOD_MS       20      /* 50 Hz, same as single EKF */
#define PRIORITY_TMR        4
#define STACK_SIZE_TMR      ( configMINIMAL_STACK_SIZE * 6 )

/* ---- Process noise variants per channel ---- */
/* Channel 0: nominal, Channel 1: slightly lower, Channel 2: slightly higher */
/* These simulate different sensor noise characteristics / calibrations */

/* ---- Telemetry decimation ---- */
#define TMR_TLM_DECIMATION  25      /* 2 Hz telemetry */

/* ---- Module state ---- */
static ekf_state_t ekf_channels[TMR_NUM_CHANNELS];
static float voted_altitude = 0.0f;
static float voted_velocity = 0.0f;
static uint8_t disagree_count = 0;
static uint8_t prev_health = 0x07;  /* track transitions to avoid fault spam */

/* ================================================================
 * Median-of-3 voter
 *
 * Returns the median value. This naturally rejects a single
 * outlier channel (the faulty one), making it ideal for TMR.
 * ================================================================ */
float tmr_vote_median3(float a, float b, float c)
{
    if (a > b) {
        if (b > c) return b;        /* a > b > c */
        else if (a > c) return c;   /* a > c >= b */
        else return a;              /* c >= a > b */
    } else {
        if (a > c) return a;        /* b >= a > c */
        else if (b > c) return c;   /* b > c >= a */
        else return b;              /* c >= b >= a */
    }
}

/* ================================================================
 * TMR EKF Task
 *
 * Reads IMU data, feeds it to all 3 EKF channels, then votes
 * on the output and reports disagreements.
 * ================================================================ */
static void task_tmr_ekf(void *params)
{
    (void)params;

    /* Initialize all three EKF channels */
    for (uint8_t i = 0; i < TMR_NUM_CHANNELS; i++) {
        ekf_init(&ekf_channels[i]);
    }

    wdg_slot_t wdg = watchdog_register("TMR_EKF", 100);
    uint32_t tlm_counter = 0;

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        imu_reading_t reading;

        if (xQueuePeek(g_imu_queue, &reading, 0) == pdTRUE) {
            /* Feed the same IMU data to all 3 channels */
            for (uint8_t i = 0; i < TMR_NUM_CHANNELS; i++) {
                /*
                 * Each channel gets a slightly perturbed input to simulate
                 * independent sensor paths. Channel 0 gets raw data,
                 * channel 1 gets +5 mg offset, channel 2 gets -5 mg offset.
                 * In real hardware these would be physically separate IMUs.
                 */
                int32_t perturbed_accel = reading.accel.z;
                if (i == 1) perturbed_accel += 5;
                if (i == 2) perturbed_accel -= 5;

                ekf_update(&ekf_channels[i], perturbed_accel,
                           reading.timestamp_ms);
            }

            /* Vote on altitude and velocity using median */
            voted_altitude = tmr_vote_median3(
                ekf_channels[0].x[0],
                ekf_channels[1].x[0],
                ekf_channels[2].x[0]
            );
            voted_velocity = tmr_vote_median3(
                ekf_channels[0].x[1],
                ekf_channels[1].x[1],
                ekf_channels[2].x[1]
            );

            /* Check agreement between channels */
            uint8_t health = 0;
            int32_t voted_mm = (int32_t)(voted_altitude * 1000.0f);

            for (uint8_t i = 0; i < TMR_NUM_CHANNELS; i++) {
                int32_t chan_mm = (int32_t)(ekf_channels[i].x[0] * 1000.0f);
                int32_t diff = chan_mm - voted_mm;
                if (diff < 0) diff = -diff;

                if (diff <= TMR_AGREE_THRESH_MM) {
                    health |= (1U << i);
                }
            }

            /* If any channel disagrees, log it (only on transition) */
            if (health != 0x07) {
                disagree_count++;

                /* Only report fault on new disagreement (not every cycle) */
                if (prev_health == 0x07) {
                    for (uint8_t i = 0; i < TMR_NUM_CHANNELS; i++) {
                        if (!(health & (1U << i))) {
                            fault_record(FAULT_SEV_WARNING, FAULT_SRC_TMR,
                                         FAULT_DETAIL_TMR_OUTVOTE, i);
                        }
                    }
                }
            }
            prev_health = health;
        }

        /* Send TMR telemetry at decimated rate */
        tlm_counter++;
        if (tlm_counter >= TMR_TLM_DECIMATION && ekf_channels[0].initialized) {
            tlm_counter = 0;

            tlm_tmr_t pkt;
            pkt.timestamp_ms = (uint32_t)xTaskGetTickCount();
            pkt.voted_alt_mm = (int32_t)(voted_altitude * 1000.0f);
            pkt.voted_vel_mms = (int32_t)(voted_velocity * 1000.0f);

            for (uint8_t i = 0; i < TMR_NUM_CHANNELS; i++) {
                pkt.chan_alt_mm[i] = (int32_t)(ekf_channels[i].x[0] * 1000.0f);
            }

            /* Recompute health for telemetry */
            uint8_t h = 0;
            for (uint8_t i = 0; i < TMR_NUM_CHANNELS; i++) {
                int32_t diff = pkt.chan_alt_mm[i] - pkt.voted_alt_mm;
                if (diff < 0) diff = -diff;
                if (diff <= TMR_AGREE_THRESH_MM) h |= (1U << i);
            }

            pkt.chan_health = h;
            pkt.disagree_count = disagree_count;
            pkt.pad[0] = 0;
            pkt.pad[1] = 0;

            tlm_send(TLM_MSG_TMR, &pkt, sizeof(pkt));
        }

        watchdog_checkin(wdg);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TMR_PERIOD_MS));
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void tmr_ekf_init(void)
{
    xTaskCreate(task_tmr_ekf, "TMR_EKF", STACK_SIZE_TMR,
                NULL, PRIORITY_TMR, NULL);
}

float tmr_get_voted_altitude(void)
{
    return voted_altitude;
}

float tmr_get_voted_velocity(void)
{
    return voted_velocity;
}
