# ---- ARM Cortex-M3 bare-metal toolchain for CMake ----
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  cortex-m3)

# Toolchain binaries
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER  arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

# Skip CMake's compiler-test link (bare-metal has no hosted C library)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# CPU / ABI flags shared by C and ASM
set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")

set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS} -ffunction-sections -fdata-sections -Wall")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS}")

# Linker: gc-sections to strip unused code, nosys for newlib stubs
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${MCU_FLAGS} -specs=nosys.specs -specs=nano.specs -Wl,--gc-sections")
