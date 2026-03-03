/*
 * Telemetry framing protocol for PROJECT-SENTINEL
 *
 * Packet format (spacecraft-style binary downlink):
 *
 *   Byte 0:  SYNC_0  (0xEB)
 *   Byte 1:  SYNC_1  (0x90)
 *   Byte 2:  MSG_ID  (message type identifier)
 *   Byte 3:  LENGTH  (payload length, 0-255)
 *   Byte 4+: PAYLOAD (LENGTH bytes of message-specific data)
 *   Last:    CKSUM   (XOR of bytes 2..end-of-payload)
 *
 * All multi-byte fields are little-endian (native Cortex-M3 order).
 */

#ifndef TLM_FRAME_H
#define TLM_FRAME_H

#include <stdint.h>
#include <stddef.h>

/* ---- Sync pattern ---- */
#define TLM_SYNC_0      0xEB
#define TLM_SYNC_1      0x90

/* ---- Message IDs ---- */
#define TLM_MSG_HEARTBEAT   0x01
#define TLM_MSG_IMU         0x02

/* ---- Max payload size ---- */
#define TLM_MAX_PAYLOAD     128

/* ---- Heartbeat payload (12 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint32_t beat_count;
    uint32_t tick_ms;
    uint8_t  task_count;
    uint8_t  pad[3];
} tlm_heartbeat_t;

/* ---- IMU payload (28 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    int32_t  accel_x;       /* milli-g */
    int32_t  accel_y;
    int32_t  accel_z;
    int32_t  gyro_x;        /* milli-deg/s */
    int32_t  gyro_y;
    int32_t  gyro_z;
} tlm_imu_t;

/*
 * Send a framed telemetry packet over UART.
 * Builds the sync + header, computes checksum, and transmits.
 */
void tlm_send(uint8_t msg_id, const void *payload, uint8_t len);

/*
 * Send a framed packet and also print a hex dump for debugging.
 * Useful when running under QEMU where the terminal is text-based.
 */
void tlm_send_debug(uint8_t msg_id, const void *payload, uint8_t len);

#endif /* TLM_FRAME_H */
