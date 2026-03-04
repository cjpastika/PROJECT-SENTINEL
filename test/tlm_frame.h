/* Mock tlm_frame.h for host unit tests */
#ifndef TLM_FRAME_H
#define TLM_FRAME_H

#include <stdint.h>

#define TLM_SYNC_0      0xEB
#define TLM_SYNC_1      0x90
#define TLM_MSG_HEARTBEAT   0x01
#define TLM_MSG_IMU         0x02
#define TLM_MAX_PAYLOAD     128

static inline void tlm_send(uint8_t id, const void *p, uint8_t l)
    { (void)id; (void)p; (void)l; }
static inline void tlm_send_debug(uint8_t id, const void *p, uint8_t l)
    { (void)id; (void)p; (void)l; }

#endif /* TLM_FRAME_H */
