/*
 * Event/Fault Manager for PROJECT-SENTINEL
 *
 * Centralized module that records anomalies with timestamps.
 * Each event has a severity level, source ID, and detail code.
 *
 * Events are stored in a fixed-size ring buffer and can be
 * dumped over UART or queried for fault counts by severity.
 *
 * Severity levels (flight software convention):
 *   INFO:     nominal events (state transitions, boot milestones)
 *   WARNING:  off-nominal but recoverable (sensor glitch, queue full)
 *   ERROR:    significant anomaly requiring attention (deadline miss)
 *   CRITICAL: system-threatening (stack overflow, malloc fail)
 */

#ifndef FAULT_MGR_H
#define FAULT_MGR_H

#include <stdint.h>

/* ---- Severity levels ---- */
typedef enum {
    FAULT_SEV_INFO,
    FAULT_SEV_WARNING,
    FAULT_SEV_ERROR,
    FAULT_SEV_CRITICAL,
    FAULT_SEV_COUNT
} fault_severity_t;

/* ---- Event source IDs ---- */
#define FAULT_SRC_SYSTEM        0x00
#define FAULT_SRC_WATCHDOG      0x01
#define FAULT_SRC_SENSOR        0x02
#define FAULT_SRC_CMD           0x03
#define FAULT_SRC_FSM           0x04
#define FAULT_SRC_EKF           0x05
#define FAULT_SRC_DATALOG       0x06
#define FAULT_SRC_RTOS          0x07

/* ---- Detail codes (per-source) ---- */
/* System */
#define FAULT_DETAIL_BOOT               0x00
#define FAULT_DETAIL_SCHED_START        0x01
#define FAULT_DETAIL_MALLOC_FAIL        0x10
#define FAULT_DETAIL_STACK_OVERFLOW     0x11
/* Watchdog */
#define FAULT_DETAIL_WDG_MISS           0x20
/* Sensor */
#define FAULT_DETAIL_SENSOR_RANGE       0x30
#define FAULT_DETAIL_QUEUE_FULL         0x31
/* FSM */
#define FAULT_DETAIL_FSM_TRANSITION     0x40
#define FAULT_DETAIL_FSM_INVALID        0x41
/* EKF */
#define FAULT_DETAIL_EKF_DIVERGE        0x50

/* ---- Max events in ring buffer ---- */
#define FAULT_MAX_EVENTS        32

/* ---- Event record ---- */
typedef struct __attribute__((packed)) {
    uint32_t tick_ms;           /* when the event occurred */
    uint8_t  severity;          /* fault_severity_t */
    uint8_t  source;            /* FAULT_SRC_* */
    uint8_t  detail;            /* FAULT_DETAIL_* */
    uint8_t  data;              /* extra context byte */
} fault_event_t;

/* ---- Telemetry ---- */
#define TLM_MSG_FAULT_EVENT     0x0A
#define TLM_MSG_FAULT_SUMMARY   0x0B

/* Fault summary telemetry payload */
typedef struct __attribute__((packed)) {
    uint32_t total_events;
    uint32_t counts[FAULT_SEV_COUNT]; /* per-severity counts */
    uint32_t tick_ms;
} tlm_fault_summary_t;

/* ---- API ---- */

/* Initialize the fault manager */
void fault_mgr_init(void);

/*
 * Record a fault event.
 * Can be called from any task or ISR context (ISR-safe version available).
 */
void fault_record(fault_severity_t sev, uint8_t source,
                  uint8_t detail, uint8_t data);

/* Dump all stored events over UART */
void fault_dump(void);

/* Get total event count */
uint32_t fault_get_total(void);

/* Get count for a specific severity */
uint32_t fault_get_count(fault_severity_t sev);

/* Clear all stored events and counters */
void fault_clear(void);

/* Send a fault summary telemetry packet */
void fault_send_summary(void);

#endif /* FAULT_MGR_H */
