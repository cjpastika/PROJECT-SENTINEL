/*
 * Startup code for LM3S6965 Cortex-M3 (QEMU target)
 *
 * Sets up the vector table, initializes .data and .bss,
 * then calls main(). Also provides default fault handlers.
 */

#include <stdint.h>

/* Symbols from linker script */
extern uint32_t _estack;
extern uint32_t _etext;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

/* Forward declarations */
extern int main(void);
void Reset_Handler(void);
void Default_Handler(void);

/* Cortex-M3 fault handlers — weakly aliased to Default_Handler */
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/*
 * Vector table — placed at the start of flash by the linker script.
 * FreeRTOS will override SVC_Handler, PendSV_Handler, SysTick_Handler
 * at link time since we declare them weak here.
 */
__attribute__((section(".isr_vector")))
const uint32_t isr_vector[] = {
    (uint32_t)&_estack,          /*  0: Initial stack pointer */
    (uint32_t)Reset_Handler,     /*  1: Reset */
    (uint32_t)NMI_Handler,       /*  2: NMI */
    (uint32_t)HardFault_Handler, /*  3: Hard fault */
    (uint32_t)MemManage_Handler, /*  4: Memory management fault */
    (uint32_t)BusFault_Handler,  /*  5: Bus fault */
    (uint32_t)UsageFault_Handler,/*  6: Usage fault */
    0, 0, 0, 0,                  /*  7-10: Reserved */
    (uint32_t)SVC_Handler,       /* 11: SVCall */
    (uint32_t)DebugMon_Handler,  /* 12: Debug monitor */
    0,                           /* 13: Reserved */
    (uint32_t)PendSV_Handler,    /* 14: PendSV */
    (uint32_t)SysTick_Handler,   /* 15: SysTick */
};

void Reset_Handler(void)
{
    /* Copy .data from flash to SRAM */
    uint32_t *src = &_etext;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Zero .bss */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    /* Call main — FreeRTOS scheduler should never return */
    main();

    /* If main returns, hang */
    while (1) {}
}

void Default_Handler(void)
{
    while (1) {}
}
