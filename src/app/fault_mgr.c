/*
 * Event/Fault Manager — centralized anomaly recording
 *
 * Stores events in a ring buffer with timestamps and severity.
 * Each event is also sent as a binary telemetry packet and
 * written to the flash data log for post-flight analysis.
 *
 * Thread-safe via FreeRTOS critical sections (suitable for
 * both task and ISR context).
 */

#include "FreeRTOS.h"
#include "task.h"

#include "hal_uart.h"
#include "fault_mgr.h"
#include "tlm_frame.h"
#include "datalog.h"

/* ---- Ring buffer ---- */
static fault_event_t event_ring[FAULT_MAX_EVENTS];
static uint32_t ring_head = 0;     /* next write position */
static uint32_t ring_count = 0;    /* number of valid entries */

/* ---- Per-severity counters ---- */
static uint32_t sev_counts[FAULT_SEV_COUNT];
static uint32_t total_events = 0;

/* ---- Severity name strings ---- */
static const char * const sev_names[] = {
    "INFO", "WARN", "ERROR", "CRIT"
};

/* ---- Utility: send hex byte ---- */
static void send_hex8(uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    hal_uart_send_char(hex[(b >> 4) & 0xF]);
    hal_uart_send_char(hex[b & 0xF]);
}

static void send_uint32(uint32_t val)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0) {
            buf[--i] = '0' + (char)(val % 10);
            val /= 10;
        }
    }
    hal_uart_send_string(&buf[i]);
}

/* ================================================================
 * Init / Clear
 * ================================================================ */

void fault_mgr_init(void)
{
    fault_clear();
}

void fault_clear(void)
{
    taskENTER_CRITICAL();
    ring_head = 0;
    ring_count = 0;
    total_events = 0;
    for (uint8_t i = 0; i < FAULT_SEV_COUNT; i++) {
        sev_counts[i] = 0;
    }
    for (uint32_t i = 0; i < FAULT_MAX_EVENTS; i++) {
        event_ring[i].tick_ms  = 0;
        event_ring[i].severity = 0;
        event_ring[i].source   = 0;
        event_ring[i].detail   = 0;
        event_ring[i].data     = 0;
    }
    taskEXIT_CRITICAL();
}

/* ================================================================
 * Record
 * ================================================================ */

void fault_record(fault_severity_t sev, uint8_t source,
                  uint8_t detail, uint8_t data)
{
    if (sev >= FAULT_SEV_COUNT) return;

    fault_event_t evt;
    evt.tick_ms  = (uint32_t)xTaskGetTickCount();
    evt.severity = (uint8_t)sev;
    evt.source   = source;
    evt.detail   = detail;
    evt.data     = data;

    /* Store in ring buffer (critical section for ISR safety) */
    taskENTER_CRITICAL();
    event_ring[ring_head] = evt;
    ring_head = (ring_head + 1) % FAULT_MAX_EVENTS;
    if (ring_count < FAULT_MAX_EVENTS) ring_count++;
    sev_counts[sev]++;
    total_events++;
    taskEXIT_CRITICAL();

    /* Log to UART */
    hal_uart_send_string("[FAULT:");
    hal_uart_send_string(sev_names[sev]);
    hal_uart_send_string("] src=0x");
    send_hex8(source);
    hal_uart_send_string(" det=0x");
    send_hex8(detail);
    hal_uart_send_string(" data=0x");
    send_hex8(data);
    hal_uart_send_string(" t=");
    send_uint32(evt.tick_ms);
    hal_uart_send_string("ms\n");

    /* Send as telemetry packet */
    tlm_send_debug(TLM_MSG_FAULT_EVENT, &evt, sizeof(evt));
}

/* ================================================================
 * Dump — print all stored events
 * ================================================================ */

void fault_dump(void)
{
    hal_uart_send_string("\n=== FAULT LOG ===\n");
    hal_uart_send_string("[FAULT] Total events: ");
    send_uint32(total_events);
    hal_uart_send_string(" (");
    for (uint8_t s = 0; s < FAULT_SEV_COUNT; s++) {
        if (s > 0) hal_uart_send_string(", ");
        hal_uart_send_string(sev_names[s]);
        hal_uart_send_string(":");
        send_uint32(sev_counts[s]);
    }
    hal_uart_send_string(")\n");

    taskENTER_CRITICAL();
    uint32_t count = ring_count;
    /* Calculate start position */
    uint32_t start;
    if (count < FAULT_MAX_EVENTS) {
        start = 0;
    } else {
        start = ring_head;  /* oldest entry is at head after wrap */
    }
    taskEXIT_CRITICAL();

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start + i) % FAULT_MAX_EVENTS;
        fault_event_t *e = &event_ring[idx];

        hal_uart_send_string("  [");
        send_uint32(i);
        hal_uart_send_string("] t=");
        send_uint32(e->tick_ms);
        hal_uart_send_string("ms ");
        if (e->severity < FAULT_SEV_COUNT) {
            hal_uart_send_string(sev_names[e->severity]);
        }
        hal_uart_send_string(" src=0x");
        send_hex8(e->source);
        hal_uart_send_string(" det=0x");
        send_hex8(e->detail);
        hal_uart_send_string(" data=0x");
        send_hex8(e->data);
        hal_uart_send_string("\n");
    }

    hal_uart_send_string("=== END FAULT LOG ===\n\n");
}

/* ================================================================
 * Summary telemetry
 * ================================================================ */

void fault_send_summary(void)
{
    tlm_fault_summary_t pkt;
    pkt.total_events = total_events;
    for (uint8_t i = 0; i < FAULT_SEV_COUNT; i++) {
        pkt.counts[i] = sev_counts[i];
    }
    pkt.tick_ms = (uint32_t)xTaskGetTickCount();

    tlm_send_debug(TLM_MSG_FAULT_SUMMARY, &pkt, sizeof(pkt));
}

/* ================================================================
 * Getters
 * ================================================================ */

uint32_t fault_get_total(void)
{
    return total_events;
}

uint32_t fault_get_count(fault_severity_t sev)
{
    if (sev >= FAULT_SEV_COUNT) return 0;
    return sev_counts[sev];
}
