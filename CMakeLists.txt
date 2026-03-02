cmake_minimum_required(VERSION 3.20)

project(PROJECT_SENTINEL
    VERSION 0.1.0
    LANGUAGES C ASM
    DESCRIPTION "Simulated flight computer on FreeRTOS / QEMU Cortex-M3"
)

# ----- Paths -----
set(FREERTOS_DIR ${CMAKE_SOURCE_DIR}/third_party/FreeRTOS-Kernel)
set(SRC_DIR      ${CMAKE_SOURCE_DIR}/src)

# ----- Linker script -----
set(LINKER_SCRIPT ${SRC_DIR}/bsp/lm3s6965.ld)

# =====================================================================
# FreeRTOS Kernel library
# =====================================================================
add_library(freertos_kernel STATIC
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    # Cortex-M3 port
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    # Heap implementation (heap_4: coalescing allocator)
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)

target_include_directories(freertos_kernel PUBLIC
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3
    # FreeRTOSConfig.h lives here
    ${SRC_DIR}
)

# =====================================================================
# Main firmware ELF
# =====================================================================
add_executable(sentinel.elf
    # Startup / BSP
    ${SRC_DIR}/bsp/startup_lm3s6965.c
    # Drivers
    ${SRC_DIR}/drivers/uart_lm3s6965.c
    ${SRC_DIR}/drivers/gpio_lm3s6965.c
    # Application
    ${SRC_DIR}/app/main.c
)

target_include_directories(sentinel.elf PRIVATE
    ${SRC_DIR}
    ${SRC_DIR}/hal
    ${SRC_DIR}/app
)

target_link_libraries(sentinel.elf PRIVATE
    freertos_kernel
)

# Linker script
target_link_options(sentinel.elf PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/sentinel.map
)

# =====================================================================
# Post-build: binary, size report
# =====================================================================
add_custom_command(TARGET sentinel.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:sentinel.elf> sentinel.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:sentinel.elf>
    COMMENT "Generating binary and size report"
)

# =====================================================================
# Convenience target: run on QEMU
# =====================================================================
add_custom_target(run
    COMMAND qemu-system-arm
        -machine lm3s6965evb
        -nographic
        -kernel $<TARGET_FILE:sentinel.elf>
    DEPENDS sentinel.elf
    COMMENT "Running sentinel on QEMU lm3s6965evb (Ctrl-A X to quit)"
    USES_TERMINAL
)
