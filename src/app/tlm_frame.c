/*
 * Telemetry framing — packet builder and transmitter
 *
 * Constructs binary telemetry packets with sync word, message ID,
 * length, payload, and XOR checksum, then sends them over UART.
 *
 * Also provides a hex-dump debug mode for QEMU terminal readability.
 */

#include "tlm_frame.h"
#include "hal_uart.h"
#include "datalog.h"
#include "ccsds.h"

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

    /* Sync word */
    hal_uart_send_char((char)TLM_SYNC_0);
    hal_uart_send_char((char)TLM_SYNC_1);

    /* Header */
    hal_uart_send_char((char)msg_id);
    hal_uart_send_char((char)len);

    /* Payload */
    hal_uart_send_bytes(p, len);

    /* Checksum */
    uint8_t cksum = compute_checksum(msg_id, p, len);
    hal_uart_send_char((char)cksum);

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
