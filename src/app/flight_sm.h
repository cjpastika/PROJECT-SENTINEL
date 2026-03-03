/*
 * Flight state machine for PROJECT-SENTINEL
 *
 * Manages mission phases with event-driven transitions:
 *
 *   IDLE  ──(arm cmd)──>  ARMED
 *   ARMED ──(accel>thr)─> BOOST
 *   BOOST ──(accel<thr)─> COAST
 *   COAST ──(accel<neg)─> DESCENT
 *   DESCENT ─(low dyn)──> LANDED
 *
 * The state machine runs as a FreeRTOS task that reads the latest
 * IMU data from the sensor queue and evaluates transition conditions.
 */

#ifndef FLIGHT_SM_H
#define FLIGHT_SM_H

#include <stdint.h>

/* ---- Flight states ---- */
typedef enum {
    FLIGHT_IDLE,        /* Pre-flight, waiting for arm command */
    FLIGHT_ARMED,       /* Armed, waiting for launch detect */
    FLIGHT_BOOST,       /* Powered ascent (high acceleration) */
    FLIGHT_COAST,       /* Unpowered flight (microgravity) */
    FLIGHT_DESCENT,     /* Falling under drag */
    FLIGHT_LANDED,      /* On the ground, mission complete */
    FLIGHT_STATE_COUNT
} flight_state_t;

/* ---- Telemetry message ID ---- */
#define TLM_MSG_FLIGHT_STATE    0x06

/* ---- Flight state telemetry payload ---- */
typedef struct __attribute__((packed)) {
    uint8_t  state;             /* current flight_state_t */
    uint8_t  prev_state;        /* state we transitioned from */
    uint16_t pad;
    uint32_t tick_ms;           /* time of last transition */
    int32_t  accel_z_mg;        /* Z accel that triggered transition */
} tlm_flight_state_t;

/* Get the current flight state */
flight_state_t flight_sm_get_state(void);

/* Send an ARM command to the state machine */
void flight_sm_arm(void);

/* Initialize and start the flight state machine task */
void flight_sm_init(void);

#endif /* FLIGHT_SM_H */
