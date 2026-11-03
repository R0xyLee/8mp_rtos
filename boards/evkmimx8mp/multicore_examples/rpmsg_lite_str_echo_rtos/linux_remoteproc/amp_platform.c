/* SPDX-License-Identifier: BSD-3-Clause */
/* OK8MP Linux remoteproc support; shared clocks and RDC belong to A53. */
#include "fsl_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "amp_platform.h"
#include "fsl_uart.h"
#include "fsl_clock.h"
#include "pin_mux.h"
#include "semphr.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Read through the A53 DTCM alias when no M7 serial console is connected. */
volatile struct amp_diagnostics amp_diagnostics;

void amp_assert_failed(const char *file, uint32_t line)
{
    __disable_irq();
    amp_diagnostics.assert_file = (uint32_t)file;
    amp_diagnostics.assert_line = line;
    for (;;)
        __WFI();
}

__attribute__((used, noreturn))
void amp_hardfault(uint32_t *stack)
{
    __disable_irq();
    amp_diagnostics.cfsr = SCB->CFSR;
    amp_diagnostics.hfsr = SCB->HFSR;
    amp_diagnostics.mmfar = SCB->MMFAR;
    amp_diagnostics.bfar = SCB->BFAR;
    amp_diagnostics.fault_pc = stack[6];
    amp_diagnostics.fault_lr = stack[5];
    for (;;)
        __WFI();
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b amp_hardfault\n");
}

static void init_memory(void)
{
    SCB_DisableICache();
    /* Reset leaves cache tags undefined: never clean a disabled D-cache. */
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_DisableDCache();
    ARM_MPU_Disable();

    /* Peripherals are device memory; TCM and shared DDR are normal memory. */
    MPU->RBAR = ARM_MPU_RBAR(0, 0x00000000U);
    MPU->RASR = ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 0, 0, 0, 1, 0,
                            ARM_MPU_REGION_SIZE_1GB);
    MPU->RBAR = ARM_MPU_RBAR(1, 0x00000000U);
    MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 1, 0, 0, 0, 0,
                            ARM_MPU_REGION_SIZE_128KB);
    MPU->RBAR = ARM_MPU_RBAR(2, 0x20000000U);
    MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 1, 0, 0, 0, 0,
                            ARM_MPU_REGION_SIZE_128KB);
    /* Both vrings and RPMsg buffers must remain non-cacheable. */
    MPU->RBAR = ARM_MPU_RBAR(3, 0x40000000U);
    MPU->RASR = ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 1, 0, 0, 0,
                            ARM_MPU_REGION_SIZE_1GB);
    MPU->RBAR = ARM_MPU_RBAR(4, 0x80000000U);
    MPU->RASR = ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 1, 0, 0, 0,
                            ARM_MPU_REGION_SIZE_16MB);
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_HFNMIENA_Msk);
    SCB_EnableICache();
    SCB_EnableDCache();
}

char amp_trace[AMP_TRACE_SIZE];
static size_t trace_used;
static SemaphoreHandle_t log_mutex;

void AMP_Log(const char *format, ...)
{
    char line[256];
    va_list args;
    size_t length, discard;
    int count;

    /* Only called from main/tasks, never from interrupt or fault handlers. */
    configASSERT(xSemaphoreTake(log_mutex, portMAX_DELAY) == pdTRUE);
    va_start(args, format);
    count = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (count > 0) {
        length = (size_t)count < sizeof(line) ? (size_t)count : sizeof(line) - 1;
        /* Keep complete recent lines in the Linux remoteproc trace window. */
        while (trace_used + length >= sizeof(amp_trace)) {
            char *end = memchr(amp_trace, '\n', trace_used);
            discard = end ? (size_t)(end - amp_trace) + 1 : trace_used;
            memmove(amp_trace, amp_trace + discard, trace_used - discard);
            trace_used -= discard;
        }
        memcpy(amp_trace + trace_used, line, length);
        trace_used += length;
        amp_trace[trace_used] = '\0';
        __DMB();
        configASSERT(UART_WriteBlocking(UART4, (const uint8_t *)line, length) == kStatus_Success);
        amp_diagnostics.uart_bytes += length;
    }
    configASSERT(xSemaphoreGive(log_mutex) == pdTRUE);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    amp_assert_failed(name, 0xffffffffU);
}

void AMP_Init(void)
{
    uart_config_t config;

    amp_diagnostics.magic = 0x414d5031;
    amp_diagnostics.phase = 1;
    init_memory();
    amp_diagnostics.phase = 2;
    SystemCoreClockUpdate();

    /* UART4 and its pads are reserved for M7 in Linux/TF-A. No shared PLL,
     * AHB, AUDIOMIX or RDC reconfiguration is allowed after Linux boots. */
    CLOCK_EnableClock(kCLOCK_Iomux);
    BOARD_InitBootPins();
    CLOCK_SetRootMux(kCLOCK_RootUart4, kCLOCK_UartRootmuxOsc24M);
    CLOCK_SetRootDivider(kCLOCK_RootUart4, 1U, 1U);
    CLOCK_EnableRoot(kCLOCK_RootUart4);
    UART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200;
    config.enableTx = true;
    config.enableRx = true;
    config.enableRxRTS = false;
    config.enableTxCTS = false;
    configASSERT(UART_Init(UART4, &config, 24000000U) == kStatus_Success);
    log_mutex = xSemaphoreCreateMutex();
    configASSERT(log_mutex != NULL);
    amp_diagnostics.phase = 3;
    AMP_Log("\r\n[M7] OK8MP FreeRTOS RPMsg demo; core=%u Hz; UART4=115200 8N1\r\n",
            (unsigned)SystemCoreClock);
}
