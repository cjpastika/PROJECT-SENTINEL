/*
 * Flight state machine — mission mode manager
 *
 * Runs a FreeRTOS task at 10 Hz that evaluates the latest IMU data
 * and drives state transitions. Each transition emits a telemetry
 * packet and a UART log message.
 *
 * Transition logic:
 *   IDLE  → ARMED:   explicit arm command (flight_sm_arm())
 *   ARMED → BOOST:   Z accel > LAUNCH_THRESHOLD for LAUNCH_CONFIRM samples
 *   BOOST → COAST:   Z accel < COAST_THRESHOLD for COAST_CONFIRM samples
 *   COAST → DESCENT: Z accel < DESCENT_THRESHOLD for DESC_CONFIRM samples
 *   DESCENT → LANDED: |accel_z - 1g| < LANDED_THRESHOLD for LAND_CONFIRM samples
 *
 * The confirmation counters prevent spurious transitions from noise.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "hal_uart.h"
#include "hal_sensor.h"
#include "flight_sm.h"
#include "task_sensor.h"
#include "tlm_frame.h"
#include "task_watchdog.h"
#include "fault_mgr.h"

/* ---- Configuration ---- */
#define SM_EVAL_PERIOD_MS       100     /* 10 Hz evaluation rate */
#define PRIORITY_FLIGHT_SM      5       /* highest app priority */
#define STACK_SIZE_SM           ( configMINIMAL_STACK_SIZE * 3 )

/* ---- Transition thresholds (milli-g) ---- */
#define LAUNCH_THRESHOLD        2000    /* Z accel > 2g = launch detect */
#define COAST_THRESHOLD         200     /* Z accel < 0.2g = engine cutoff */
#define DESCENT_THRESHOLD       (-300)  /* Z accel < -0.3g = drag onset */
#define LANDED_GRAVITY_CENTER   1000    /* expected 1g on ground */
#define LANDED_TOLERANCE        200     /* |az - 1g| < 200 mg */

/* ---- Confirmation counts (debounce) ---- */
#define LAUNCH_CONFIRM          3       /* 3 consecutive samples (300 ms) */
#define COAST_CONFIRM           5       /* 5 consecutive samples (500 ms) */
#define DESC_CONFIRM            3
#define LAND_CONFIRM            10      /* 10 samples (1 sec) of calm */

/* ---- State names for logging ---- */
static const char * const state_names[] = {
    "IDLE", "ARMED", "BOOST", "COAST", "DESCENT", "LANDED"
};

/* ---- Module state ---- */
static volatile flight_state_t current_state = FLIGHT_IDLE;
static flight_state_t prev_state = FLIGHT_IDLE;
static uint32_t confirm_count = 0;
static int32_t  last_accel_z  = 0;
static volatile uint8_t arm_requested = 0;

/* ---- Emit state transition telemetry ---- */
static void emit_transition(flight_state_t from, flight_state_t to, int32_t accel_z)
{
    tlm_flight_state_t pkt;
    pkt.state      = (uint8_t)to;
    pkt.prev_state = (uint8_t)from;
    pkt.pad        = 0;
    pkt.tick_ms    = (uint32_t)xTaskGetTickCount();
    pkt.accel_z_mg = accel_z;

    tlm_send(TLM_MSG_FLIGHT_STATE, &pkt, sizeof(pkt));
}

/* ---- Transition helper ---- */
static void transition_to(flight_state_t new_state, int32_t accel_z)
{
    prev_state = current_state;
    emit_transition(current_state, new_state, accel_z);
    fault_record(FAULT_SEV_INFO, FAULT_SRC_FSM,
                 FAULT_DETAIL_FSM_TRANSITION, (uint8_t)new_state);
    current_state = new_state;
    confirm_count = 0;
}

/* ================================================================
 * State evaluation — called every SM_EVAL_PERIOD_MS
 * ================================================================ */
static void evaluate_state(int32_t accel_z)
{
    last_accel_z = accel_z;

    switch (current_state) {
    case FLIGHT_IDLE:
        /* Wait for explicit arm command */
        if (arm_requested) {
            arm_requested = 0;
            transition_to(FLIGHT_ARMED, accel_z);
        }
        break;

    case FLIGHT_ARMED:
        /* Detect launch: sustained high Z acceleration */
        if (accel_z > LAUNCH_THRESHOLD) {
            confirm_count++;
            if (confirm_count >= LAUNCH_CONFIRM) {
                transition_to(FLIGHT_BOOST, accel_z);
            }
        } else {
            confirm_count = 0;
        }
        break;

    case FLIGHT_BOOST:
        /* Detect engine cutoff: Z accel drops below coast threshold */
        if (accel_z < COAST_THRESHOLD) {
            confirm_count++;
            if (confirm_count >= COAST_CONFIRM) {
                transition_to(FLIGHT_COAST, accel_z);
            }
        } else {
            confirm_count = 0;
        }
        break;

    case FLIGHT_COAST:
        /* Detect descent onset: negative Z accel (drag) */
        if (accel_z < DESCENT_THRESHOLD) {
            confirm_count++;
            if (confirm_count >= DESC_CONFIRM) {
                transition_to(FLIGHT_DESCENT, accel_z);
            }
        } else {
            confirm_count = 0;
        }
        break;

    case FLIGHT_DESCENT:
        /* Detect landing: accel settles near 1g */
        {
            int32_t diff = accel_z - LANDED_GRAVITY_CENTER;
            if (diff < 0) diff = -diff;
            if (diff < LANDED_TOLERANCE) {
                confirm_count++;
                if (confirm_count >= LAND_CONFIRM) {
                    transition_to(FLIGHT_LANDED, accel_z);
                }
            } else {
                confirm_count = 0;
            }
        }
        break;

    case FLIGHT_LANDED:
        /* Terminal state — mission complete */
        break;

    default:
        break;
    }
}

/* ================================================================
 * Flight State Machine Task
 *
 * Reads the latest IMU sample from the shared queue (peek, not
 * consume) and evaluates state transitions at 10 Hz.
 * ================================================================ */
static void task_flight_sm(void *params)
{
    (void)params;

    /* Auto-arm after 2 seconds for demo purposes */
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (current_state == FLIGHT_IDLE) {
        flight_sm_arm();
    }

    /* Register watchdog after initial delay so it doesn't fault during startup */
    wdg_slot_t wdg = watchdog_register("FLIGHT_SM", 300);

    for (;;) {
        /* Read latest IMU sample from the queue (non-destructive peek) */
        imu_reading_t reading;
        if (xQueuePeek(g_imu_queue, &reading, 0) == pdTRUE) {
            evaluate_state(reading.accel.z);
        }

        watchdog_checkin(wdg);
        vTaskDelay(pdMS_TO_TICKS(SM_EVAL_PERIOD_MS));
    }
}

/* ================================================================
 * Public API
 * ================================================================ */
flight_state_t flight_sm_get_state(void)
{
    return current_state;
}

void flight_sm_arm(void)
{
    arm_requested = 1;
}

void flight_sm_init(void)
{
    xTaskCreate(task_flight_sm, "FLIGHT_SM", STACK_SIZE_SM,
                NULL, PRIORITY_FLIGHT_SM, NULL);
}
