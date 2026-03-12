/*
 * Simulated flash data logger
 *
 * Uses a static RAM buffer to simulate a flash memory region.
 * Telemetry packets are stored in a circular buffer with a small
 * header per entry. When the buffer wraps, oldest data is lost.
 *
 * The dump command replays all valid entries as framed telemetry
 * packets, allowing post-flight data retrieval over UART.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "hal_uart.h"
#include "datalog.h"
#include "tlm_frame.h"

/* ---- Simulated flash memory ---- */
static uint8_t flash_buffer[DATALOG_FLASH_SIZE];
static uint32_t write_pos = 0;
static datalog_stats_t stats;
static uint8_t autolog_enabled = 1;

/* Mutex for thread-safe access */
static SemaphoreHandle_t log_mutex = NULL;

/* ---- Utility: send decimal number over UART ---- */
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
 * Init / Erase
 * ================================================================ */

void datalog_init(void)
{
    log_mutex = xSemaphoreCreateMutex();
    configASSERT(log_mutex);
    datalog_erase();
}

void datalog_erase(void)
{
    if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY);

    for (uint32_t i = 0; i < DATALOG_FLASH_SIZE; i++) {
        flash_buffer[i] = 0xFF;  /* erased flash = 0xFF */
    }
    write_pos = 0;
    stats.entries_written = 0;
    stats.bytes_used = 0;
    stats.wrap_count = 0;

    if (log_mutex) xSemaphoreGive(log_mutex);

    hal_uart_send_string("[LOG] Flash erased\n");
}

/* ================================================================
 * Write
 * ================================================================ */

void datalog_write(uint8_t msg_id, const void *payload, uint8_t len)
{
    if (len > DATALOG_MAX_PAYLOAD) return;

    uint32_t entry_size = sizeof(datalog_entry_hdr_t) + len;
    if (entry_size > DATALOG_FLASH_SIZE) return;

    if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY);

    /* Check if we need to wrap */
    if (write_pos + entry_size > DATALOG_FLASH_SIZE) {
        write_pos = 0;
        stats.wrap_count++;
    }

    /* Write header */
    datalog_entry_hdr_t hdr;
    hdr.magic[0] = DATALOG_MAGIC_0;
    hdr.magic[1] = DATALOG_MAGIC_1;
    hdr.msg_id   = msg_id;
    hdr.length   = len;
    hdr.tick_ms  = (uint32_t)xTaskGetTickCount();

    const uint8_t *hdr_bytes = (const uint8_t *)&hdr;
    for (uint32_t i = 0; i < sizeof(hdr); i++) {
        flash_buffer[write_pos + i] = hdr_bytes[i];
    }

    /* Write payload */
    const uint8_t *src = (const uint8_t *)payload;
    for (uint8_t i = 0; i < len; i++) {
        flash_buffer[write_pos + sizeof(hdr) + i] = src[i];
    }

    write_pos += entry_size;
    stats.entries_written++;
    stats.bytes_used = write_pos;

    if (log_mutex) xSemaphoreGive(log_mutex);
}

/* ================================================================
 * Dump — replay all valid entries over UART
 * ================================================================ */

void datalog_dump(void)
{
    if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY);

    hal_uart_send_string("\n=== LOG DUMP START ===\n");
    hal_uart_send_string("[LOG] Entries written: ");
    send_uint32(stats.entries_written);
    hal_uart_send_string(", Wraps: ");
    send_uint32(stats.wrap_count);
    hal_uart_send_string(", Used: ");
    send_uint32(stats.bytes_used);
    hal_uart_send_string("/");
    send_uint32(DATALOG_FLASH_SIZE);
    hal_uart_send_string(" bytes\n");

    /* Scan the buffer for valid entries */
    uint32_t pos = 0;
    uint32_t count = 0;

    while (pos + sizeof(datalog_entry_hdr_t) <= DATALOG_FLASH_SIZE) {
        /* Check for magic bytes */
        if (flash_buffer[pos] != DATALOG_MAGIC_0 ||
            flash_buffer[pos + 1] != DATALOG_MAGIC_1) {
            break;  /* No more valid entries */
        }

        /* Read header */
        datalog_entry_hdr_t hdr;
        const uint8_t *hdr_bytes = &flash_buffer[pos];
        uint8_t *dst = (uint8_t *)&hdr;
        for (uint32_t i = 0; i < sizeof(hdr); i++) {
            dst[i] = hdr_bytes[i];
        }

        uint32_t entry_size = sizeof(hdr) + hdr.length;
        if (pos + entry_size > DATALOG_FLASH_SIZE) break;
        if (hdr.length > DATALOG_MAX_PAYLOAD) break;

        /* Re-send this entry as a framed telemetry packet */
        tlm_send(hdr.msg_id, &flash_buffer[pos + sizeof(hdr)], hdr.length);

        count++;
        pos += entry_size;

        /* Yield briefly to avoid starving other tasks during large dumps */
        if (count % 10 == 0) {
            if (log_mutex) xSemaphoreGive(log_mutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY);
        }
    }

    hal_uart_send_string("[LOG] Dumped ");
    send_uint32(count);
    hal_uart_send_string(" entries\n");
    hal_uart_send_string("=== LOG DUMP END ===\n\n");

    if (log_mutex) xSemaphoreGive(log_mutex);
}

/* ================================================================
 * Stats / Autolog
 * ================================================================ */

void datalog_get_stats(datalog_stats_t *out)
{
    if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY);
    *out = stats;
    if (log_mutex) xSemaphoreGive(log_mutex);
}

void datalog_set_autolog(uint8_t enable)
{
    autolog_enabled = enable;
}

uint8_t datalog_get_autolog(void)
{
    return autolog_enabled;
}
