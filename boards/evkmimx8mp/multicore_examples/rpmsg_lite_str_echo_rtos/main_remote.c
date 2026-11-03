/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "rpmsg_ns.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "rsc_table.h"

#ifdef OK8MP_LINUX_REMOTEPROC
#include "amp_platform.h"
#define APP_LOG AMP_Log
#define APP_PHASE(value) (amp_diagnostics.phase = (value))
#else
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#define APP_LOG PRINTF
#define APP_PHASE(value) ((void)0)
#endif

#define RPMSG_LITE_NS_ANNOUNCE_STRING "rpmsg-virtual-tty-channel-1"
#ifndef LOCAL_EPT_ADDR
#define LOCAL_EPT_ADDR 30U
#endif

static TaskHandle_t app_task_handle;
static volatile uint32_t rx_packets, rx_bytes;
static volatile bool link_ready;

static void app_task(void *param)
{
    struct rpmsg_lite_instance *rpmsg;
    struct rpmsg_lite_endpoint *endpoint;
    rpmsg_queue_handle queue;
    uint32_t source, length;
    char *payload;
    int32_t result;

    (void)param;
    APP_PHASE(5);
    APP_LOG("[M7] task rpmsg-echo started; waiting for A53 Linux\r\n");
    rpmsg = rpmsg_lite_remote_init((void *)VDEV0_VRING_BASE,
                                  RL_PLATFORM_IMX8MP_M7_USER_LINK_ID, RL_NO_FLAGS);
    configASSERT(rpmsg != NULL);
    APP_PHASE(6);
    while (!rpmsg_lite_is_link_up(rpmsg))
        vTaskDelay(pdMS_TO_TICKS(1));
    APP_PHASE(7);

    queue = rpmsg_queue_create(rpmsg);
    configASSERT(queue != NULL);
    endpoint = rpmsg_lite_create_ept(rpmsg, LOCAL_EPT_ADDR, rpmsg_queue_rx_cb, queue);
    configASSERT(endpoint != NULL);
    APP_PHASE(8);
    result = rpmsg_ns_announce(rpmsg, endpoint, RPMSG_LITE_NS_ANNOUNCE_STRING, RL_NS_CREATE);
    configASSERT(result == RL_SUCCESS);
    link_ready = true;
    APP_PHASE(9);
    APP_LOG("[M7] RPMsg link up; endpoint=%u; channel=%s\r\n",
            (unsigned)LOCAL_EPT_ADDR, RPMSG_LITE_NS_ANNOUNCE_STRING);

    for (;;) {
        result = rpmsg_queue_recv_nocopy(rpmsg, queue, &source, &payload, &length, RL_BLOCK);
        configASSERT(result == RL_SUCCESS);
        /* Preserve arbitrary binary payloads, including NUL and full buffers. */
        result = rpmsg_lite_send(rpmsg, endpoint, source, payload, length, RL_BLOCK);
        configASSERT(result == RL_SUCCESS);
        result = rpmsg_queue_nocopy_free(rpmsg, payload);
        configASSERT(result == RL_SUCCESS);
        taskENTER_CRITICAL();
        rx_packets++;
        rx_bytes += length;
        taskEXIT_CRITICAL();
    }
}

static void heartbeat_task(void *param)
{
    TickType_t wake = xTaskGetTickCount();
    uint32_t packets, bytes;

    (void)param;
    APP_LOG("[M7] task heartbeat started; period=1000 ms\r\n");
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        taskENTER_CRITICAL();
        packets = rx_packets;
        bytes = rx_bytes;
        taskEXIT_CRITICAL();
        APP_LOG("[M7] alive t=%u s link=%s rx=%u packets/%u bytes heap=%u "
                "stack_free=%u/%u words\r\n",
                (unsigned)(xTaskGetTickCount() / configTICK_RATE_HZ),
                link_ready ? "up" : "waiting", (unsigned)packets, (unsigned)bytes,
                (unsigned)xPortGetFreeHeapSize(),
                (unsigned)uxTaskGetStackHighWaterMark(app_task_handle),
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

int main(void)
{
#ifdef OK8MP_LINUX_REMOTEPROC
    AMP_Init();
#else
    BOARD_InitMemory();
    BOARD_RdcInit();
    BOARD_InitBootPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();
#endif
    copyResourceTable();
    configASSERT(xTaskCreate(app_task, "rpmsg-echo", 768, NULL,
                             tskIDLE_PRIORITY + 1, &app_task_handle) == pdPASS);
    configASSERT(xTaskCreate(heartbeat_task, "heartbeat", 768, NULL,
                             tskIDLE_PRIORITY + 2, NULL) == pdPASS);
    APP_PHASE(4);
    APP_LOG("[M7] starting FreeRTOS scheduler\r\n");
    vTaskStartScheduler();
    configASSERT(false);
    for (;;)
        ;
}
