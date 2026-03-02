/*
 * Linker script for LM3S6965 (Cortex-M3) on QEMU
 * 256KB Flash, 64KB SRAM
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* End of stack — top of SRAM */
_estack = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    /* Vector table + code goes into FLASH */
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.init)
        *(.fini)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* ARM exception unwinding */
    .ARM.extab :
    {
        *(.ARM.extab*)
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx*)
    } > FLASH

    /* Mark end of all flash content — .data LMA starts here */
    _etext = .;

    /* Initialized data: stored in FLASH, copied to SRAM at startup */
    .data : AT(_etext)
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM

    /* Zero-initialized data in SRAM */
    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /* Heap starts after BSS */
    _heap_start = .;
    _heap_end = ORIGIN(SRAM) + LENGTH(SRAM);
}
