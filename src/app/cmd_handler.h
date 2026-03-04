/*
 * Command handler for PROJECT-SENTINEL
 *
 * Uplink command protocol (same framing as telemetry):
 *
 *   [SYNC_0][SYNC_1][CMD_ID][LENGTH][PAYLOAD...][CKSUM]
 *    0xEB    0x90    1 byte  1 byte  0-N bytes   XOR
 *
 * Supported commands:
 *   CMD_NOOP        (0x10) — no-op, responds with ACK
 *   CMD_LED_TOGGLE  (0x11) — toggle the status LED
 *   CMD_STATUS_REQ  (0x12) — request immediate heartbeat packet
 *   CMD_SET_TLM_RATE(0x13) — set telemetry period (payload: uint16 ms)
 *
 * The command task polls UART RX for incoming bytes, synchronizes
 * on the sync word, validates the checksum, and dispatches handlers.
 * Also supports single-character shortcuts for interactive use:
 *   'l' — LED toggle,  's' — status,  '+'/'-' — adjust tlm rate
 */

#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include <stdint.h>

/* ---- Command IDs (0x10-0x1F range, separate from TLM 0x01-0x0F) ---- */
#define CMD_NOOP            0x10
#define CMD_LED_TOGGLE      0x11
#define CMD_STATUS_REQ      0x12
#define CMD_SET_TLM_RATE    0x13
#define CMD_ARM             0x14
#define CMD_LOG_DUMP        0x15
#define CMD_LOG_ERASE       0x16

/* ---- Response message IDs ---- */
#define TLM_MSG_CMD_ACK     0x03
#define TLM_MSG_CMD_NACK    0x04

/* ---- ACK/NACK payload ---- */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_id;        /* command that was processed */
    uint8_t  result;        /* 0 = success, nonzero = error code */
    uint16_t pad;
} tlm_cmd_response_t;

/* Telemetry output rate in ms — adjustable via CMD_SET_TLM_RATE or +/- keys */
extern volatile uint32_t g_tlm_rate_ms;

/* Initialize and start the command handler task */
void cmd_handler_init(void);

#endif /* CMD_HANDLER_H */
