/*
 * Simulated flash data logger for PROJECT-SENTINEL
 *
 * Provides a circular-buffer "flash" region in SRAM that records
 * telemetry packets. Supports writing, reading back, and dumping
 * all stored entries over UART for post-flight analysis.
 *
 * Storage format per entry:
 *   [MAGIC_0][MAGIC_1][MSG_ID][LENGTH][TICK_MS (4B)][PAYLOAD...]
 *
 * The buffer wraps around when full (oldest entries overwritten).
 */

#ifndef DATALOG_H
#define DATALOG_H

#include <stdint.h>
#include <stddef.h>

/* Simulated flash size — 4 KB circular buffer */
#define DATALOG_FLASH_SIZE      4096

/* Entry header magic (distinguishes valid entries from empty flash) */
#define DATALOG_MAGIC_0         0xDA
#define DATALOG_MAGIC_1         0x7A

/* Max payload per log entry */
#define DATALOG_MAX_PAYLOAD     128

/* Entry header (8 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  magic[2];      /* 0xDA 0x7A */
    uint8_t  msg_id;
    uint8_t  length;        /* payload length */
    uint32_t tick_ms;       /* timestamp */
} datalog_entry_hdr_t;

/* ---- Datalog statistics ---- */
typedef struct {
    uint32_t entries_written;   /* total entries ever written */
    uint32_t bytes_used;        /* current write pointer offset */
    uint32_t wrap_count;        /* how many times the buffer wrapped */
} datalog_stats_t;

/* ---- Telemetry message ID for log dump ---- */
#define TLM_MSG_LOG_ENTRY       0x08
#define TLM_MSG_LOG_STATUS      0x09

/* Log status telemetry payload */
typedef struct __attribute__((packed)) {
    uint32_t entries_written;
    uint32_t bytes_used;
    uint32_t wrap_count;
    uint32_t flash_size;
} tlm_log_status_t;

/* Initialize the data logger (erase simulated flash) */
void datalog_init(void);

/* Write a telemetry packet to the log */
void datalog_write(uint8_t msg_id, const void *payload, uint8_t len);

/* Dump all stored log entries over UART as framed telemetry */
void datalog_dump(void);

/* Erase all stored data */
void datalog_erase(void);

/* Get current statistics */
void datalog_get_stats(datalog_stats_t *stats);

/* Enable/disable automatic logging of all telemetry */
void datalog_set_autolog(uint8_t enable);

/* Check if autolog is enabled */
uint8_t datalog_get_autolog(void);

#endif /* DATALOG_H */
