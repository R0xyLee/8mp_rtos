cmake_minimum_required(VERSION 3.16)
project(ok8mp_m7_rpmsg C ASM)

option(OK8MP_RPMSG_TTY "Use the upstream Linux RPMsg TTY service name" OFF)

get_filename_component(MCUX_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(example ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(NOT EXISTS "${MCUX_SDK_ROOT}/devices/MIMX8ML8")
    message(FATAL_ERROR "Set MCUX_SDK_ROOT to the i.MX8MP MCUXpresso/FreeRTOS SDK")
endif()

set(MCUX_SDK_PROJECT_NAME ok8mp-m7-rpmsg.elf)
set(CONFIG_USE_middleware_freertos-kernel_MIMX8ML8 true)
add_executable(${MCUX_SDK_PROJECT_NAME} ${example}/main_remote.c ${example}/rsc_table.c
    ${example}/pin_mux.c amp_platform.c)
target_include_directories(${MCUX_SDK_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${example})
target_compile_definitions(${MCUX_SDK_PROJECT_NAME} PRIVATE
    CPU_MIMX8ML8DVNLZ FSL_RTOS_FREE_RTOS SDK_DEBUGCONSOLE=2
    OK8MP_LINUX_REMOTEPROC=1
    $<$<BOOL:${OK8MP_RPMSG_TTY}>:OK8MP_RPMSG_TTY>
    __STARTUP_CLEAR_BSS __STARTUP_INITIALIZE_NONCACHEDATA)
target_compile_options(${MCUX_SDK_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard
    -Os -g -Wall -Wno-address-of-packed-member
    -ffunction-sections -fdata-sections -fno-common -ffreestanding)
target_link_options(${MCUX_SDK_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections -Wl,-Map=ok8mp-m7-rpmsg.map
    -Wl,--defsym=__stack_size__=0x800 -Wl,--defsym=__heap_size__=0x400
    -T${example}/armgcc/MIMX8ML8xxxxx_cm7_ram.ld)
set_property(TARGET ${MCUX_SDK_PROJECT_NAME} APPEND PROPERTY
    LINK_DEPENDS ${example}/armgcc/MIMX8ML8xxxxx_cm7_ram.ld)

set(device ${MCUX_SDK_ROOT}/devices/MIMX8ML8)
set(freertos ${MCUX_SDK_ROOT}/rtos/freertos/freertos_kernel)
set(rpmsg ${MCUX_SDK_ROOT}/middleware/multicore/rpmsg_lite/lib)
target_sources(${MCUX_SDK_PROJECT_NAME} PRIVATE
    ${device}/gcc/startup_MIMX8ML8_cm7.S
    ${device}/system_MIMX8ML8_cm7.c
    ${device}/drivers/fsl_common.c
    ${device}/drivers/fsl_clock.c
    ${device}/drivers/fsl_mu.c
    ${device}/drivers/fsl_uart.c
    ${freertos}/event_groups.c ${freertos}/list.c ${freertos}/queue.c
    ${freertos}/stream_buffer.c ${freertos}/tasks.c ${freertos}/timers.c
    ${freertos}/portable/GCC/ARM_CM4F/port.c
    ${freertos}/portable/MemMang/heap_4.c
    ${rpmsg}/common/llist.c ${rpmsg}/virtio/virtqueue.c
    ${rpmsg}/rpmsg_lite/rpmsg_lite.c ${rpmsg}/rpmsg_lite/rpmsg_ns.c
    ${rpmsg}/rpmsg_lite/rpmsg_queue.c
    ${rpmsg}/rpmsg_lite/porting/environment/rpmsg_env_freertos.c
    ${rpmsg}/rpmsg_lite/porting/platform/imx8mp_m7/rpmsg_platform.c)
target_include_directories(${MCUX_SDK_PROJECT_NAME} PRIVATE
    ${device} ${device}/drivers ${device}/utilities/debug_console_lite
    ${MCUX_SDK_ROOT}/CMSIS/Include
    ${freertos}/include ${freertos}/portable/GCC/ARM_CM4F
    ${rpmsg}/include ${rpmsg}/include/platform/imx8mp_m7)
target_link_libraries(${MCUX_SDK_PROJECT_NAME} PRIVATE m)
