/*
 * Command handler — uplink command processing task
 *
 * Runs a FreeRTOS task that polls UART RX for incoming bytes.
 * Two input modes are supported:
 *
 *   1. Binary framed commands (same protocol as telemetry):
 *      [0xEB][0x90][CMD_ID][LEN][PAYLOAD...][CKSUM]
 *
 *   2. Single-character shortcuts (for interactive QEMU terminal):
 *      'l' = LED toggle, 's' = status request, '+'/'-' = adjust rate
 *
 * Responses are sent as framed telemetry packets (ACK/NACK).
 */

#include "FreeRTOS.h"
#include "task.h"

#include "hal_uart.h"
#include "hal_gpio.h"
#include "cmd_handler.h"
#include "tlm_frame.h"
#include "task_sensor.h"
#include "task_watchdog.h"
#include "flight_sm.h"
#include "datalog.h"

/* ---- Configuration ---- */
#define CMD_POLL_PERIOD_MS      50      /* check for input at 20 Hz */
#define PRIORITY_CMD_HANDLER    3       /* above heartbeat, below sensor sampling */
#define STACK_SIZE_CMD          ( configMINIMAL_STACK_SIZE * 3 )

#define CMD_MAX_PAYLOAD         32

/* ---- Shared telemetry rate (ms), adjustable via command ---- */
volatile uint32_t g_tlm_rate_ms = 500;

/* ---- Send ACK/NACK response ---- */
static void send_ack(uint8_t cmd_id, uint8_t result)
{
    tlm_cmd_response_t resp;
    resp.cmd_id = cmd_id;
    resp.result = result;
    resp.pad    = 0;

    uint8_t msg_type = (result == 0) ? TLM_MSG_CMD_ACK : TLM_MSG_CMD_NACK;
    tlm_send_debug(msg_type, &resp, sizeof(resp));
}

/* ---- Command handlers ---- */

static void handle_noop(void)
{
    hal_uart_send_string("[CMD] NOOP received\n");
    send_ack(CMD_NOOP, 0);
}

static void handle_led_toggle(void)
{
    hal_led_toggle();
    hal_uart_send_string("[CMD] LED toggled\n");
    send_ack(CMD_LED_TOGGLE, 0);
}

static void handle_status_req(void)
{
    hal_uart_send_string("[CMD] Status requested\n");

    /* Send an immediate heartbeat-style response */
    tlm_heartbeat_t hb;
    hb.beat_count = 0xFFFFFFFF;  /* marker: on-demand, not periodic */
    hb.tick_ms    = (uint32_t)xTaskGetTickCount();
    hb.task_count = (uint8_t)uxTaskGetNumberOfTasks();
    hb.pad[0] = 0;
    hb.pad[1] = 0;
    hb.pad[2] = 0;

    tlm_send_debug(TLM_MSG_HEARTBEAT, &hb, sizeof(hb));
    send_ack(CMD_STATUS_REQ, 0);
}

static void handle_set_tlm_rate(const uint8_t *payload, uint8_t len)
{
    if (len < 2) {
        hal_uart_send_string("[CMD] SET_TLM_RATE: bad length\n");
        send_ack(CMD_SET_TLM_RATE, 1);
        return;
    }

    uint16_t new_rate = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);

    /* Clamp to reasonable range: 100 ms - 5000 ms */
    if (new_rate < 100)  new_rate = 100;
    if (new_rate > 5000) new_rate = 5000;

    g_tlm_rate_ms = new_rate;
    hal_uart_send_string("[CMD] TLM rate updated\n");
    send_ack(CMD_SET_TLM_RATE, 0);
}

static void handle_arm(void)
{
    flight_sm_arm();
    hal_uart_send_string("[CMD] ARM command sent\n");
    send_ack(CMD_ARM, 0);
}

static void handle_log_dump(void)
{
    hal_uart_send_string("[CMD] Dumping flash log\n");
    send_ack(CMD_LOG_DUMP, 0);
    datalog_dump();
}

static void handle_log_erase(void)
{
    datalog_erase();
    hal_uart_send_string("[CMD] Flash log erased\n");
    send_ack(CMD_LOG_ERASE, 0);
}

/* ---- Dispatch a validated command ---- */
static void dispatch_command(uint8_t cmd_id, const uint8_t *payload, uint8_t len)
{
    switch (cmd_id) {
    case CMD_NOOP:
        handle_noop();
        break;
    case CMD_LED_TOGGLE:
        handle_led_toggle();
        break;
    case CMD_STATUS_REQ:
        handle_status_req();
        break;
    case CMD_SET_TLM_RATE:
        handle_set_tlm_rate(payload, len);
        break;
    case CMD_ARM:
        handle_arm();
        break;
    case CMD_LOG_DUMP:
        handle_log_dump();
        break;
    case CMD_LOG_ERASE:
        handle_log_erase();
        break;
    default:
        hal_uart_send_string("[CMD] Unknown command\n");
        send_ack(cmd_id, 0xFF);
        break;
    }
}

/* ---- Single-character shortcut handler ---- */
static void handle_shortcut(char c)
{
    switch (c) {
    case 'l':
    case 'L':
        dispatch_command(CMD_LED_TOGGLE, NULL, 0);
        break;
    case 's':
    case 'S':
        dispatch_command(CMD_STATUS_REQ, NULL, 0);
        break;
    case '+':
        /* Decrease period = faster telemetry */
        if (g_tlm_rate_ms > 100) {
            g_tlm_rate_ms -= 100;
        }
        hal_uart_send_string("[CMD] TLM rate: faster\n");
        send_ack(CMD_SET_TLM_RATE, 0);
        break;
    case '-':
        /* Increase period = slower telemetry */
        if (g_tlm_rate_ms < 5000) {
            g_tlm_rate_ms += 100;
        }
        hal_uart_send_string("[CMD] TLM rate: slower\n");
        send_ack(CMD_SET_TLM_RATE, 0);
        break;
    case 'a':
    case 'A':
        dispatch_command(CMD_ARM, NULL, 0);
        break;
    case 'd':
    case 'D':
        dispatch_command(CMD_LOG_DUMP, NULL, 0);
        break;
    case 'e':
    case 'E':
        dispatch_command(CMD_LOG_ERASE, NULL, 0);
        break;
    case '?':
        hal_uart_send_string("\n--- Command Shortcuts ---\n");
        hal_uart_send_string("  a  Arm flight SM\n");
        hal_uart_send_string("  d  Dump flash log\n");
        hal_uart_send_string("  e  Erase flash log\n");
        hal_uart_send_string("  l  LED toggle\n");
        hal_uart_send_string("  s  Status request\n");
        hal_uart_send_string("  +  Faster telemetry\n");
        hal_uart_send_string("  -  Slower telemetry\n");
        hal_uart_send_string("  ?  This help\n");
        hal_uart_send_string("-------------------------\n");
        break;
    default:
        /* Ignore unknown characters (CR, LF, etc.) */
        break;
    }
}

/* ---- Parser state machine for binary framed commands ---- */
typedef enum {
    PARSE_IDLE,
    PARSE_SYNC1,
    PARSE_CMD_ID,
    PARSE_LENGTH,
    PARSE_PAYLOAD,
    PARSE_CKSUM
} parse_state_t;

/* ================================================================
 * Command Handler Task
 *
 * Polls UART RX and processes incoming bytes through a state
 * machine that detects either binary framed commands or
 * single-character shortcuts.
 * ================================================================ */
static void task_cmd_handler(void *params)
{
    (void)params;
    wdg_slot_t wdg = watchdog_register("CMD_HANDLER", 200);

    parse_state_t state = PARSE_IDLE;
    uint8_t cmd_id   = 0;
    uint8_t pay_len  = 0;
    uint8_t pay_idx  = 0;
    uint8_t payload[CMD_MAX_PAYLOAD];

    for (;;) {
        /* Poll for available bytes */
        while (hal_uart_rx_ready()) {
            uint8_t byte = (uint8_t)hal_uart_recv_char();

            switch (state) {
            case PARSE_IDLE:
                if (byte == TLM_SYNC_0) {
                    state = PARSE_SYNC1;
                } else {
                    /* Not a sync byte — treat as shortcut character */
                    handle_shortcut((char)byte);
                }
                break;

            case PARSE_SYNC1:
                if (byte == TLM_SYNC_1) {
                    state = PARSE_CMD_ID;
                } else {
                    /* False sync — the first byte might have been a shortcut */
                    handle_shortcut((char)TLM_SYNC_0);
                    if (byte == TLM_SYNC_0) {
                        state = PARSE_SYNC1;
                    } else {
                        handle_shortcut((char)byte);
                        state = PARSE_IDLE;
                    }
                }
                break;

            case PARSE_CMD_ID:
                cmd_id = byte;
                state = PARSE_LENGTH;
                break;

            case PARSE_LENGTH:
                pay_len = byte;
                pay_idx = 0;
                if (pay_len == 0) {
                    state = PARSE_CKSUM;
                } else if (pay_len > CMD_MAX_PAYLOAD) {
                    /* Too long — abort */
                    hal_uart_send_string("[CMD] Payload too long\n");
                    state = PARSE_IDLE;
                } else {
                    state = PARSE_PAYLOAD;
                }
                break;

            case PARSE_PAYLOAD:
                payload[pay_idx++] = byte;
                if (pay_idx >= pay_len) {
                    state = PARSE_CKSUM;
                }
                break;

            case PARSE_CKSUM: {
                /* Verify XOR checksum: cmd_id ^ len ^ payload bytes */
                uint8_t expected = cmd_id ^ pay_len;
                for (uint8_t i = 0; i < pay_len; i++) {
                    expected ^= payload[i];
                }
                if (byte == expected) {
                    dispatch_command(cmd_id, payload, pay_len);
                } else {
                    hal_uart_send_string("[CMD] Checksum error\n");
                    send_ack(cmd_id, 0xFE);
                }
                state = PARSE_IDLE;
                break;
            }
            }
        }

        watchdog_checkin(wdg);
        vTaskDelay(pdMS_TO_TICKS(CMD_POLL_PERIOD_MS));
    }
}

/* ================================================================
 * Public init
 * ================================================================ */
void cmd_handler_init(void)
{
    xTaskCreate(task_cmd_handler, "CMD_HANDLER", STACK_SIZE_CMD,
                NULL, PRIORITY_CMD_HANDLER, NULL);
}
