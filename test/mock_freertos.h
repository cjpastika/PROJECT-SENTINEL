/*
 * Mock FreeRTOS types for host-compiled unit tests.
 * Only provides type definitions — no real RTOS functionality.
 */

#ifndef MOCK_FREERTOS_H
#define MOCK_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

/* ---- Basic FreeRTOS types ---- */
typedef uint32_t TickType_t;
typedef int32_t  BaseType_t;
typedef void*    TaskHandle_t;
typedef void*    QueueHandle_t;

#define pdTRUE          1
#define pdFALSE         0
#define pdPASS          1

#define configMINIMAL_STACK_SIZE    128
#define tskKERNEL_VERSION_NUMBER    "MOCK"

/* ---- Tick count mock ---- */
static uint32_t mock_tick_count = 0;
static inline TickType_t xTaskGetTickCount(void) { return mock_tick_count; }
static inline void mock_set_tick(uint32_t t) { mock_tick_count = t; }

#define pdMS_TO_TICKS(ms) (ms)

/* ---- Stubs that do nothing ---- */
#define vTaskDelay(x)          ((void)(x))
#define vTaskDelayUntil(a, b)  ((void)(a), (void)(b))
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#define taskDISABLE_INTERRUPTS()

static inline BaseType_t xQueuePeek(QueueHandle_t q, void *buf, TickType_t wait)
{
    (void)q; (void)buf; (void)wait;
    return pdFALSE;
}

static inline BaseType_t xTaskCreate(void *fn, const char *name,
    uint16_t stack, void *params, uint32_t prio, void *handle)
{
    (void)fn; (void)name; (void)stack; (void)params; (void)prio; (void)handle;
    return pdPASS;
}

static inline uint32_t uxTaskGetNumberOfTasks(void) { return 1; }

#endif /* MOCK_FREERTOS_H */
