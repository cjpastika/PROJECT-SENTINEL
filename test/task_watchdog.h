/* Mock task_watchdog.h for host unit tests */
#ifndef TASK_WATCHDOG_H
#define TASK_WATCHDOG_H

#include <stdint.h>

#define WDG_MAX_TASKS 8

typedef int8_t wdg_slot_t;

static inline wdg_slot_t watchdog_register(const char *n, uint32_t d)
    { (void)n; (void)d; return 0; }
static inline void watchdog_checkin(wdg_slot_t s) { (void)s; }
static inline void watchdog_init(void) {}

static volatile uint32_t g_wdg_fault_count = 0;

#endif /* TASK_WATCHDOG_H */
