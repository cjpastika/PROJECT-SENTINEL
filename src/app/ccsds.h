/*
 * CCSDS-lite Space Packet Protocol for PROJECT-SENTINEL
 *
 * Simplified implementation of the CCSDS Space Packet Protocol
 * (CCSDS 133.0-B-2) used by NASA, ESA, and JAXA for spacecraft
 * telemetry and telecommand framing.
 *
 * Primary header (6 bytes):
 *   Bits [15:13]  Version Number    (always 000 = Version 1)
 *   Bit  [12]     Packet Type       (0 = TLM, 1 = CMD)
 *   Bit  [11]     Sec Header Flag   (1 = secondary header present)
 *   Bits [10:0]   APID              (Application Process Identifier)
 *   Bits [15:14]  Sequence Flags    (11 = standalone packet)
 *   Bits [13:0]   Sequence Count    (14-bit rolling counter)
 *   Bits [15:0]   Data Length       (num_octets_in_data_field - 1)
 *
 * Secondary header (4 bytes, optional):
 *   Bits [31:0]   Timestamp (ms since boot)
 *
 * This "lite" variant:
 *   - Always uses standalone packets (seq flags = 0b11)
 *   - Always includes 4-byte secondary header (timestamp)
 *   - Uses 16-bit CRC-CCITT as error detection code (appended)
 *   - Maps existing MSG_IDs to APIDs
 */

#ifndef CCSDS_H
#define CCSDS_H

#include <stdint.h>

/* ---- CCSDS primary header (6 bytes, big-endian) ---- */
typedef struct __attribute__((packed)) {
    uint16_t packet_id;         /* version(3) | type(1) | sec_hdr(1) | apid(11) */
    uint16_t packet_seq;        /* seq_flags(2) | seq_count(14) */
    uint16_t data_length;       /* total data field octets - 1 */
} ccsds_primary_hdr_t;

/* ---- CCSDS secondary header (4 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;      /* milliseconds since boot */
} ccsds_secondary_hdr_t;

/* ---- Version / field constants ---- */
#define CCSDS_VERSION       0       /* Version 1 */
#define CCSDS_TYPE_TLM      0       /* Telemetry */
#define CCSDS_TYPE_CMD      1       /* Telecommand */
#define CCSDS_SEC_HDR_ON    1       /* Secondary header present */
#define CCSDS_SEQ_STANDALONE 3      /* Sequence flags: standalone */

/* ---- APID assignments (maps to existing MSG_IDs) ---- */
#define CCSDS_APID_HEARTBEAT    0x001
#define CCSDS_APID_IMU          0x002
#define CCSDS_APID_CMD_ACK      0x003
#define CCSDS_APID_CMD_NACK     0x004
#define CCSDS_APID_WDG_FAULT    0x005
#define CCSDS_APID_FLIGHT_STATE 0x006
#define CCSDS_APID_EKF          0x007
#define CCSDS_APID_LOG_ENTRY    0x008
#define CCSDS_APID_LOG_STATUS   0x009
#define CCSDS_APID_FAULT_EVENT  0x00A
#define CCSDS_APID_FAULT_SUMM   0x00B
#define CCSDS_APID_TMR          0x00C
#define CCSDS_APID_IDLE         0x7FF   /* CCSDS idle packet */

/* ---- Telemetry message for CCSDS stats ---- */
#define TLM_MSG_CCSDS_STATS 0x0D

typedef struct __attribute__((packed)) {
    uint32_t packets_sent;      /* total CCSDS packets transmitted */
    uint32_t bytes_sent;        /* total bytes (including headers) */
    uint16_t seq_count;         /* current sequence counter */
    uint16_t crc_errors;        /* CRC mismatches detected on uplink */
    uint32_t timestamp_ms;
} tlm_ccsds_stats_t;

/*
 * Initialize the CCSDS framing layer.
 * Must be called before any CCSDS packet transmission.
 */
void ccsds_init(void);

/*
 * Send a telemetry payload wrapped in a CCSDS space packet.
 *   apid:    Application Process Identifier (use CCSDS_APID_*)
 *   payload: raw payload data
 *   len:     payload length in bytes
 *
 * Builds primary header + secondary header (timestamp) + payload + CRC-16.
 * Transmits over UART.
 */
void ccsds_send_tlm(uint16_t apid, const void *payload, uint8_t len);

/*
 * Enable/disable CCSDS framing mode.
 * When enabled, tlm_send() also outputs CCSDS-framed packets.
 */
void ccsds_set_enabled(uint8_t enable);
uint8_t ccsds_get_enabled(void);

/*
 * Get the current CCSDS packet statistics.
 */
void ccsds_get_stats(tlm_ccsds_stats_t *out);

/*
 * Compute CRC-16/CCITT over a byte buffer.
 * Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
 * Initial value: 0xFFFF
 */
uint16_t ccsds_crc16(const uint8_t *data, uint16_t len);

#endif /* CCSDS_H */
