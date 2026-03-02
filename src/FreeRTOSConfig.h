/*
 * FreeRTOS configuration for PROJECT-SENTINEL
 * Target: LM3S6965 Cortex-M3 on QEMU
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ----- Core scheduler settings ----- */
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      ( 50000000UL )  /* 50 MHz — QEMU default for lm3s6965 */
#define configTICK_RATE_HZ                      ( 1000 )        /* 1 ms tick */
#define configMAX_PRIORITIES                    ( 8 )
#define configMINIMAL_STACK_SIZE                ( 128 )         /* words (512 bytes) */
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* ----- Memory allocation ----- */
#define configTOTAL_HEAP_SIZE                   ( 32 * 1024 )   /* 32 KB for FreeRTOS heap */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* ----- Hook functions ----- */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2   /* Method 2: paints stack with known value */

/* ----- Runtime stats and tracing ----- */
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configGENERATE_RUN_TIME_STATS           0

/* ----- Co-routine (unused, but must define) ----- */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )

/* ----- Software timer ----- */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/* ----- Mutex / semaphore features ----- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8

/* ----- INCLUDE functions ----- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTimerPendFunctionCall          1

/* ----- Cortex-M3 interrupt priority configuration ----- */
/*
 * The Cortex-M3 uses 3 priority bits on LM3S6965 (8 levels, 0-7).
 * Priority values are shifted left by 5 to fill the top bits of the
 * 8-bit priority register (ARM convention).
 *
 * Interrupts at or above configMAX_SYSCALL_INTERRUPT_PRIORITY can
 * NOT call FreeRTOS API functions (they're "high priority" / real-time).
 * Interrupts below that threshold CAN use FromISR() API calls safely.
 */
#define configPRIO_BITS                         3
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
#define configKERNEL_INTERRUPT_PRIORITY          ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* ----- Map FreeRTOS port handler names to our vector table names ----- */
#define vPortSVCHandler       SVC_Handler
#define xPortPendSVHandler    PendSV_Handler
#define xPortSysTickHandler   SysTick_Handler

/* ----- Assert ----- */
#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

#endif /* FREERTOS_CONFIG_H */
