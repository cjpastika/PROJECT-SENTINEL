/*
 * Triple Modular Redundancy (TMR) Voting for PROJECT-SENTINEL
 *
 * Aerospace-standard fault tolerance pattern: run N independent
 * computation channels and vote on the output. A disagreement
 * indicates a computation fault (bit-flip, divergence, etc.).
 *
 * This module provides:
 *   - Generic median-of-3 voter for float values
 *   - Per-channel health tracking with agreement thresholds
 *   - Fault reporting via the fault manager
 *   - Telemetry for TMR status
 *
 * Applied to the EKF altitude estimator: three independent EKF
 * instances run with slightly different process noise parameters,
 * and the voted altitude/velocity is used as the system output.
 */

#ifndef TMR_H
#define TMR_H

#include <stdint.h>

/* ---- Number of redundant channels ---- */
#define TMR_NUM_CHANNELS    3

/* ---- Agreement threshold (mm) — channels within this are "agreeing" ---- */
#define TMR_AGREE_THRESH_MM     2000    /* 2 m — accounts for channel perturbation divergence */

/* ---- Telemetry ---- */
#define TLM_MSG_TMR         0x0C

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    int32_t  voted_alt_mm;          /* median-voted altitude */
    int32_t  voted_vel_mms;         /* median-voted velocity */
    int32_t  chan_alt_mm[TMR_NUM_CHANNELS];  /* per-channel altitudes */
    uint8_t  chan_health;           /* bitmask: bit N = channel N healthy */
    uint8_t  disagree_count;       /* running disagreement counter (wraps) */
    uint8_t  pad[2];
} tlm_tmr_t;

/* ---- Fault detail codes ---- */
#define FAULT_SRC_TMR           0x08
#define FAULT_DETAIL_TMR_DISAGREE   0x60
#define FAULT_DETAIL_TMR_OUTVOTE    0x61

/*
 * Median-of-3 voter for float values.
 * Returns the median of a, b, c.
 */
float tmr_vote_median3(float a, float b, float c);

/*
 * Initialize the TMR EKF subsystem.
 * Creates 3 independent EKF instances with varied process noise
 * and starts the TMR voting task.
 */
void tmr_ekf_init(void);

/*
 * Get the voted (median) altitude in meters.
 */
float tmr_get_voted_altitude(void);

/*
 * Get the voted (median) velocity in m/s.
 */
float tmr_get_voted_velocity(void);

#endif /* TMR_H */
