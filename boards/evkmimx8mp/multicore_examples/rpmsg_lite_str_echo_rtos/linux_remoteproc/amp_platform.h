/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef OK8MP_AMP_PLATFORM_H
#define OK8MP_AMP_PLATFORM_H

#include <stdint.h>

#define AMP_TRACE_SIZE 4096U
extern char amp_trace[AMP_TRACE_SIZE];

struct amp_diagnostics {
    uint32_t magic, phase, assert_line, assert_file;
    uint32_t cfsr, hfsr, mmfar, bfar, fault_pc, fault_lr;
    uint32_t uart_bytes;
};
extern volatile struct amp_diagnostics amp_diagnostics;

void AMP_Init(void);
void AMP_Log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
