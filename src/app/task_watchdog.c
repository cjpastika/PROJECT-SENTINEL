/*
 * Task watchdog — software timer-based deadline monitor
 *
 * A FreeRTOS software timer fires every WDG_CHECK_PERIOD_MS and
 * inspects each registered task slot. If a task hasn't checked in
 * within its declared deadline, a fault is logged via UART and
 * the global fault counter is incremented.
 *
 * The watchdog uses tick counts (not wall-clock time) so it works
 * correctly under QEMU's variable-speed emulation.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "hal_uart.h"
#include "task_watchdog.h"
#include "tlm_frame.h"
#include "fault_mgr.h"

/* ---- Configuration ---- */
#define WDG_CHECK_PERIOD_MS     500     /* how often the timer fires */

/* ---- Per-task watchdog slot ---- */
typedef struct {
    const char *name;           /* task name for logging */
    uint32_t    deadline_ms;    /* max ms between check-ins */
    TickType_t  last_checkin;   /* tick count of last check-in */
    uint8_t     active;         /* 1 if slot is in use */
    uint8_t     faulted;        /* 1 if currently in fault state */
} wdg_slot_entry_t;

/* ---- Module state ---- */
static wdg_slot_entry_t slots[WDG_MAX_TASKS];
static uint8_t          slot_count = 0;
static TimerHandle_t    wdg_timer  = NULL;

volatile uint32_t g_wdg_fault_count = 0;

/* ---- Telemetry message for watchdog fault ---- */
#define TLM_MSG_WDG_FAULT   0x05

typedef struct __attribute__((packed)) {
    uint8_t  slot_id;
    uint8_t  pad;
    uint16_t missed_ms;         /* how many ms overdue */
    uint32_t tick_ms;           /* current tick */
} tlm_wdg_fault_t;

/* ================================================================
 * Timer callback — runs in the timer daemon task context
 *
 * Scans all active slots and checks if any task has exceeded
 * its deadline since its last check-in.
 * ================================================================ */
static void wdg_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    TickType_t now = xTaskGetTickCount();

    for (uint8_t i = 0; i < slot_count; i++) {
        if (!slots[i].active) continue;

        TickType_t elapsed = now - slots[i].last_checkin;
        uint32_t elapsed_ms = (uint32_t)(elapsed * 1000 / configTICK_RATE_HZ);

        if (elapsed_ms > slots[i].deadline_ms) {
            /* Deadline missed! */
            if (!slots[i].faulted) {
                /* First detection — log the fault */
                slots[i].faulted = 1;
                g_wdg_fault_count++;

                /* Record in fault manager */
                fault_record(FAULT_SEV_ERROR, FAULT_SRC_WATCHDOG,
                             FAULT_DETAIL_WDG_MISS, i);

                /* Send binary watchdog telemetry */
                tlm_wdg_fault_t pkt;
                pkt.slot_id   = i;
                pkt.pad       = 0;
                pkt.missed_ms = (elapsed_ms > 0xFFFF) ? 0xFFFF : (uint16_t)elapsed_ms;
                pkt.tick_ms   = (uint32_t)now;
                tlm_send(TLM_MSG_WDG_FAULT, &pkt, sizeof(pkt));
            }
        } else {
            /* Task is healthy — clear fault flag if it recovered */
            slots[i].faulted = 0;
        }
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

wdg_slot_t watchdog_register(const char *name, uint32_t deadline_ms)
{
    if (slot_count >= WDG_MAX_TASKS) {
        hal_uart_send_string("[WDG] No slots available!\n");
        return -1;
    }

    wdg_slot_t id = (wdg_slot_t)slot_count;
    slots[id].name         = name;
    slots[id].deadline_ms  = deadline_ms;
    slots[id].last_checkin = xTaskGetTickCount();
    slots[id].active       = 1;
    slots[id].faulted      = 0;
    slot_count++;

    return id;
}

void watchdog_checkin(wdg_slot_t slot)
{
    if (slot >= 0 && slot < WDG_MAX_TASKS && slots[slot].active) {
        slots[slot].last_checkin = xTaskGetTickCount();
    }
}

void watchdog_init(void)
{
    wdg_timer = xTimerCreate(
        "WDG_TIMER",
        pdMS_TO_TICKS(WDG_CHECK_PERIOD_MS),
        pdTRUE,         /* auto-reload */
        NULL,
        wdg_timer_callback
    );
    configASSERT(wdg_timer);

    xTimerStart(wdg_timer, 0);
}
