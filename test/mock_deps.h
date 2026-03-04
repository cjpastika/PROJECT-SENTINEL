/*
 * Mock application dependencies for host-compiled tests.
 * Stubs for modules that are not under test.
 *
 * NOTE: This header uses static functions/variables so it can be
 * included in multiple translation units without linker conflicts.
 */

#ifndef MOCK_DEPS_H
#define MOCK_DEPS_H

#include <stdint.h>

/* ---- Watchdog stubs ---- */
typedef int8_t wdg_slot_t;
static inline wdg_slot_t watchdog_register(const char *n, uint32_t d)
    { (void)n; (void)d; return 0; }
static inline void watchdog_checkin(wdg_slot_t s) { (void)s; }
static inline void watchdog_init(void) {}

/* ---- task_watchdog.h guard to prevent real header inclusion ---- */
#define TASK_WATCHDOG_H

/* ---- Telemetry stubs ---- */
static inline void tlm_send(uint8_t id, const void *p, uint8_t l)
    { (void)id; (void)p; (void)l; }
static inline void tlm_send_debug(uint8_t id, const void *p, uint8_t l)
    { (void)id; (void)p; (void)l; }
#define TLM_FRAME_H  /* prevent real header inclusion */

/* ---- Datalog stubs ---- */
static inline void datalog_init(void) {}
static inline void datalog_write(uint8_t id, const void *d, uint8_t l)
    { (void)id; (void)d; (void)l; }
static inline uint8_t datalog_get_autolog(void) { return 0; }

/* ---- Fault manager stubs (track calls for verification) ---- */
static uint32_t mock_fault_count = 0;
static uint8_t  mock_last_fault_sev = 0;
static uint8_t  mock_last_fault_src = 0;
static uint8_t  mock_last_fault_detail = 0;
static uint8_t  mock_last_fault_data = 0;

/* Fault severity enum needed by some modules */
#ifndef FAULT_MGR_H
#define FAULT_MGR_H
typedef enum {
    FAULT_SEV_INFO, FAULT_SEV_WARNING, FAULT_SEV_ERROR, FAULT_SEV_CRITICAL, FAULT_SEV_COUNT
} fault_severity_t;
#define FAULT_SRC_SYSTEM    0x00
#define FAULT_SRC_WATCHDOG  0x01
#define FAULT_SRC_SENSOR    0x02
#define FAULT_SRC_CMD       0x03
#define FAULT_SRC_FSM       0x04
#define FAULT_SRC_EKF       0x05
#define FAULT_SRC_DATALOG   0x06
#define FAULT_SRC_RTOS      0x07
#define FAULT_DETAIL_BOOT            0x00
#define FAULT_DETAIL_FSM_TRANSITION  0x40
#define FAULT_DETAIL_FSM_INVALID     0x41
#endif

static inline void fault_record(fault_severity_t sev, uint8_t src, uint8_t det, uint8_t data)
{
    mock_fault_count++;
    mock_last_fault_sev = sev;
    mock_last_fault_src = src;
    mock_last_fault_detail = det;
    mock_last_fault_data = data;
}

static inline void fault_mgr_init(void) { mock_fault_count = 0; }
static inline void mock_fault_reset(void) { mock_fault_count = 0; }

/* ---- Sensor queue stub ---- */
#include "hal_sensor.h"
#ifndef TASK_SENSOR_H
#define TASK_SENSOR_H
static QueueHandle_t g_imu_queue = NULL;
#endif

/* ---- CCSDS stubs (skipped when testing ccsds.c directly) ---- */
#ifndef MOCK_SKIP_CCSDS
static inline void ccsds_init(void) {}
static inline uint8_t ccsds_get_enabled(void) { return 0; }
static inline void ccsds_send_tlm(uint16_t a, const void *p, uint8_t l)
    { (void)a; (void)p; (void)l; }
#endif

#endif /* MOCK_DEPS_H */
