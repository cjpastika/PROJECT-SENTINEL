/*
 * Task watchdog for PROJECT-SENTINEL
 *
 * Uses a FreeRTOS software timer to monitor task health.
 * Each registered task must "check in" before its deadline
 * expires. If any task misses its deadline, a fault is logged.
 *
 * Usage:
 *   1. Call watchdog_init() at startup
 *   2. Each task calls watchdog_register() to get a slot
 *   3. Each task calls watchdog_checkin() periodically
 *   4. The watchdog timer fires at a fixed interval and
 *      checks all registered slots for missed deadlines
 */

#ifndef TASK_WATCHDOG_H
#define TASK_WATCHDOG_H

#include <stdint.h>

/* Maximum number of tasks the watchdog can monitor */
#define WDG_MAX_TASKS       8

/* Watchdog slot handle (0..WDG_MAX_TASKS-1, or -1 on failure) */
typedef int8_t wdg_slot_t;

/*
 * Register a task with the watchdog.
 *   name:        human-readable name (for fault logging)
 *   deadline_ms: maximum allowed time between check-ins
 * Returns a slot handle, or -1 if no slots available.
 */
wdg_slot_t watchdog_register(const char *name, uint32_t deadline_ms);

/*
 * Check in ("pet") the watchdog for the given slot.
 * Must be called before the deadline expires.
 */
void watchdog_checkin(wdg_slot_t slot);

/*
 * Initialize the watchdog subsystem and start the monitor timer.
 */
void watchdog_init(void);

/* Counter of total faults detected (for telemetry) */
extern volatile uint32_t g_wdg_fault_count;

#endif /* TASK_WATCHDOG_H */
