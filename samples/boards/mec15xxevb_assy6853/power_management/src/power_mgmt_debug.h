/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>

/* Instrumentation to measure latency and track entry exit via gpios
 *
 * In EVB set following jumpers:
 * JP99 7-8     closed
 * JP99 10-11   closed
 * JP75 29-30   closed
 * JP75 32-33   closed
 *
 * In EVB probe following pins:
 * JP25.3 (GPIO012_LT) light sleep
 * JP25.5 (GPIO013_DP) deep sleep
 * JP25.7 (GPIO014_TRIG) trigger in app
 * JP75.29 (GPIO60_CLK_OUT)
 */

#define LT_GPIO_REG         GPIO_CTRL_REGS->CTRL_0012
#define DP_GPIO_REG         GPIO_CTRL_REGS->CTRL_0013
#define MARKER_GPIO_REG     GPIO_CTRL_REGS->CTRL_0014

#ifdef LT_GPIO_REG
#define PM_LT_ENTER()       (LT_GPIO_REG = 0x240ul)
#define PM_LT_EXIT()        (LT_GPIO_REG = 0x10240ul)
#endif

#ifdef DP_GPIO_REG
#define PM_DP_ENTER()       (DP_GPIO_REG = 0x240ul)
#define PM_DP_EXIT()        (DP_GPIO_REG = 0x10240ul)
#endif

#ifdef MARKER_GPIO_REG
#define PM_TRIGGER_ENTER()  (MARKER_GPIO_REG = 0x240ul)
#define PM_TRIGGER_EXIT()   (MARKER_GPIO_REG = 0x10240ul)
#endif
