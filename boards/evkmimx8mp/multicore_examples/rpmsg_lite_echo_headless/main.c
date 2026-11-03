/* SPDX-License-Identifier: BSD-3-Clause */
/* Linux-loaded OK8MP M7 echo service; shared clocks and RDC belong to A53. */
#include "fsl_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "rpmsg_ns.h"
#include "rsc_table.h"
#include "amp_layout.h"

/* Read through the A53 DTCM alias when no M7 serial console is connected. */
volatile struct {
    uint32_t magic;
    uint32_t phase;
    uint32_t assert_line;
    uint32_t assert_file;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t fault_pc;
    uint32_t fault_lr;
} amp_diagnostics;

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

static void echo_task(void *arg)
{
    struct rpmsg_lite_instance *rpmsg;
    struct rpmsg_lite_endpoint *endpoint;
    rpmsg_queue_handle queue;
    uint32_t source, length;
    char *payload;
    int32_t ret;

    (void)arg;
    amp_diagnostics.phase = 5;
    rpmsg = rpmsg_lite_remote_init((void *)VDEV0_VRING_BASE,
                                  RL_PLATFORM_IMX8MP_M7_USER_LINK_ID, RL_NO_FLAGS);
    configASSERT(rpmsg != NULL);
    amp_diagnostics.phase = 6;
    while (!rpmsg_lite_is_link_up(rpmsg))
        vTaskDelay(pdMS_TO_TICKS(1));
    amp_diagnostics.phase = 7;

    queue = rpmsg_queue_create(rpmsg);
    configASSERT(queue != NULL);
    endpoint = rpmsg_lite_create_ept(rpmsg, 30, rpmsg_queue_rx_cb, queue);
    configASSERT(endpoint != NULL);
    amp_diagnostics.phase = 8;
    ret = rpmsg_ns_announce(rpmsg, endpoint, "rpmsg-virtual-tty-channel-1", RL_NS_CREATE);
    configASSERT(ret == RL_SUCCESS);
    amp_diagnostics.phase = 9;

    for (;;) {
        ret = rpmsg_queue_recv_nocopy(rpmsg, queue, &source, &payload, &length, RL_BLOCK);
        configASSERT(ret == RL_SUCCESS);
        ret = rpmsg_lite_send(rpmsg, endpoint, source, payload, length, RL_BLOCK);
        configASSERT(ret == RL_SUCCESS);
        ret = rpmsg_queue_nocopy_free(rpmsg, payload);
        configASSERT(ret == RL_SUCCESS);
    }
}

int main(void)
{
    amp_diagnostics.magic = 0x414d5031;
    amp_diagnostics.phase = 1;
    init_memory();
    amp_diagnostics.phase = 2;
    /* Read the clock configured by Linux; never retune shared PLLs or buses. */
    SystemCoreClockUpdate();
    amp_diagnostics.phase = 3;
    copyResourceTable();
    configASSERT(xTaskCreate(echo_task, "amp-echo", 512, NULL,
                             tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    amp_diagnostics.phase = 4;
    vTaskStartScheduler();
    for (;;)
        __WFI();
}
