/*
 * Telemetry framing — packet builder and transmitter
 *
 * Constructs binary telemetry packets with sync word, message ID,
 * length, payload, and XOR checksum.  Uses ARM semihosting to write
 * the entire frame in a single host trap, avoiding the per-byte MMIO
 * overhead of the UART which throttles QEMU emulation speed.
 */

#include "tlm_frame.h"
#include "hal_uart.h"
#include "datalog.h"
#include "ccsds.h"
#include <string.h>

/* ---- ARM semihosting: write buffer to stdout in one host call ---- */
static void semihost_write_buf(const uint8_t *buf, uint32_t len)
{
    /*
     * ARM semihosting SYS_WRITE (0x05):
     *   args[0] = file handle (1 = stdout)
     *   args[1] = pointer to data
     *   args[2] = length
     * Returns number of bytes NOT written (0 on success).
     */
    volatile uint32_t args[3];
    args[0] = 1;                 /* stdout */
    args[1] = (uint32_t)buf;
    args[2] = len;
    __asm volatile (
        "mov  r0, #0x05\n"      /* SYS_WRITE */
        "mov  r1, %0\n"
        "bkpt #0xAB\n"
        : /* no outputs */
        : "r" (args)
        : "r0", "r1", "memory"
    );
}

/* ---- Compute XOR checksum over msg_id + length + payload ---- */
static uint8_t compute_checksum(uint8_t msg_id, const uint8_t *payload, uint8_t len)
{
    uint8_t cksum = msg_id ^ len;
    for (uint8_t i = 0; i < len; i++) {
        cksum ^= payload[i];
    }
    return cksum;
}

void tlm_send(uint8_t msg_id, const void *payload, uint8_t len)
{
    const uint8_t *p = (const uint8_t *)payload;

    /* Auto-log to flash (skip log-replay packets to avoid recursion) */
    if (datalog_get_autolog() && msg_id != TLM_MSG_LOG_ENTRY) {
        datalog_write(msg_id, payload, len);
    }

    /* Build complete frame in a local buffer: SYNC(2) + ID(1) + LEN(1) + PAYLOAD + CKSUM(1) */
    uint8_t frame[TLM_MAX_PAYLOAD + 5];
    uint8_t pos = 0;

    frame[pos++] = TLM_SYNC_0;
    frame[pos++] = TLM_SYNC_1;
    frame[pos++] = msg_id;
    frame[pos++] = len;
    memcpy(&frame[pos], p, len);
    pos += len;
    frame[pos++] = compute_checksum(msg_id, p, len);

    /* Single semihosting call writes the entire frame at once */
    semihost_write_buf(frame, pos);

    /* Also send as CCSDS space packet (msg_id maps directly to APID) */
    if (ccsds_get_enabled()) {
        ccsds_send_tlm((uint16_t)msg_id, payload, len);
    }
}

/* ---- Hex dump helpers ---- */
static const char hex_chars[] = "0123456789ABCDEF";

static void send_hex_byte(uint8_t b)
{
    hal_uart_send_char(hex_chars[(b >> 4) & 0x0F]);
    hal_uart_send_char(hex_chars[b & 0x0F]);
}

void tlm_send_debug(uint8_t msg_id, const void *payload, uint8_t len)
{
    const uint8_t *p = (const uint8_t *)payload;
    uint8_t cksum = compute_checksum(msg_id, p, len);

    /* Print a hex-dump line: [TLM:XX] EB 90 ID LEN PAYLOAD.. CK */
    hal_uart_send_string("[TLM:");
    send_hex_byte(msg_id);
    hal_uart_send_string("] ");

    /* Sync */
    send_hex_byte(TLM_SYNC_0);
    hal_uart_send_char(' ');
    send_hex_byte(TLM_SYNC_1);
    hal_uart_send_char(' ');

    /* Header */
    send_hex_byte(msg_id);
    hal_uart_send_char(' ');
    send_hex_byte(len);

    /* Payload */
    for (uint8_t i = 0; i < len; i++) {
        hal_uart_send_char(' ');
        send_hex_byte(p[i]);
    }

    /* Checksum */
    hal_uart_send_char(' ');
    send_hex_byte(cksum);
    hal_uart_send_string("\n");

    /* Also send the raw binary packet */
    tlm_send(msg_id, payload, len);
}
