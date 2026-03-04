/*
 * CCSDS-lite Space Packet Protocol implementation
 *
 * Builds CCSDS space packets with primary + secondary headers,
 * appends CRC-16/CCITT, and transmits over UART. This provides
 * a standards-compliant framing layer alongside the project's
 * simpler custom telemetry protocol.
 *
 * When enabled, packets are sent in parallel with the existing
 * [0xEB 0x90] framed packets, allowing ground systems to use
 * either protocol.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "hal_uart.h"
#include "ccsds.h"

/* ---- Module state ---- */
static uint16_t seq_counter = 0;
static uint32_t packets_sent = 0;
static uint32_t bytes_sent = 0;
static uint8_t  ccsds_enabled = 1;

/* ================================================================
 * CRC-16/CCITT
 *
 * Standard CCSDS error detection code.
 * Polynomial: 0x1021, init: 0xFFFF, no final XOR.
 * Bit-by-bit implementation (no lookup table to save flash).
 * ================================================================ */
uint16_t ccsds_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/* ---- Helper: send a byte over UART ---- */
static void send_byte(uint8_t b)
{
    hal_uart_send_char((char)b);
}

/* ---- Helper: send big-endian uint16 ---- */
static void send_be16(uint16_t val)
{
    send_byte((uint8_t)(val >> 8));
    send_byte((uint8_t)(val & 0xFF));
}

/* ---- Helper: send big-endian uint32 ---- */
static void send_be32(uint32_t val)
{
    send_byte((uint8_t)(val >> 24));
    send_byte((uint8_t)(val >> 16));
    send_byte((uint8_t)(val >> 8));
    send_byte((uint8_t)(val & 0xFF));
}

/* ================================================================
 * Build and send a CCSDS space packet
 * ================================================================ */
void ccsds_send_tlm(uint16_t apid, const void *payload, uint8_t len)
{
    if (!ccsds_enabled) return;

    const uint8_t *p = (const uint8_t *)payload;

    /* Data field = secondary header (4) + payload (len) */
    uint16_t data_field_len = sizeof(ccsds_secondary_hdr_t) + len;
    /* CCSDS data_length field = num_octets - 1 */
    /* We add 2 for CRC that follows the data field */
    uint16_t pkt_data_length = data_field_len + 2 - 1;

    /* Build primary header fields (big-endian) */
    uint16_t packet_id = ((uint16_t)CCSDS_VERSION << 13)
                       | ((uint16_t)CCSDS_TYPE_TLM << 12)
                       | ((uint16_t)CCSDS_SEC_HDR_ON << 11)
                       | (apid & 0x7FF);

    uint16_t packet_seq = ((uint16_t)CCSDS_SEQ_STANDALONE << 14)
                        | (seq_counter & 0x3FFF);

    /* Build the complete packet in a buffer for CRC computation */
    uint8_t hdr_buf[6 + 4]; /* primary(6) + secondary(4) */
    hdr_buf[0] = (uint8_t)(packet_id >> 8);
    hdr_buf[1] = (uint8_t)(packet_id & 0xFF);
    hdr_buf[2] = (uint8_t)(packet_seq >> 8);
    hdr_buf[3] = (uint8_t)(packet_seq & 0xFF);
    hdr_buf[4] = (uint8_t)(pkt_data_length >> 8);
    hdr_buf[5] = (uint8_t)(pkt_data_length & 0xFF);

    /* Secondary header: timestamp */
    uint32_t now_ms = (uint32_t)xTaskGetTickCount();
    hdr_buf[6] = (uint8_t)(now_ms >> 24);
    hdr_buf[7] = (uint8_t)(now_ms >> 16);
    hdr_buf[8] = (uint8_t)(now_ms >> 8);
    hdr_buf[9] = (uint8_t)(now_ms & 0xFF);

    /* Compute CRC over header + payload */
    uint16_t crc = 0xFFFF;

    /* CRC over header bytes */
    for (uint8_t i = 0; i < 10; i++) {
        crc ^= (uint16_t)hdr_buf[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }

    /* CRC over payload bytes */
    for (uint8_t i = 0; i < len; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }

    /* Transmit: primary header */
    send_be16(packet_id);
    send_be16(packet_seq);
    send_be16(pkt_data_length);

    /* Transmit: secondary header (timestamp) */
    send_be32(now_ms);

    /* Transmit: payload */
    hal_uart_send_bytes(p, len);

    /* Transmit: CRC-16 */
    send_be16(crc);

    /* Update counters */
    seq_counter = (seq_counter + 1) & 0x3FFF;
    packets_sent++;
    bytes_sent += 6 + 4 + len + 2; /* primary + secondary + payload + CRC */
}

/* ================================================================
 * Public API
 * ================================================================ */

void ccsds_init(void)
{
    seq_counter = 0;
    packets_sent = 0;
    bytes_sent = 0;
    ccsds_enabled = 1;
}

void ccsds_set_enabled(uint8_t enable)
{
    ccsds_enabled = enable;
}

uint8_t ccsds_get_enabled(void)
{
    return ccsds_enabled;
}

void ccsds_get_stats(tlm_ccsds_stats_t *out)
{
    out->packets_sent = packets_sent;
    out->bytes_sent = bytes_sent;
    out->seq_count = seq_counter;
    out->crc_errors = 0;  /* no uplink parsing yet */
    out->timestamp_ms = (uint32_t)xTaskGetTickCount();
}
