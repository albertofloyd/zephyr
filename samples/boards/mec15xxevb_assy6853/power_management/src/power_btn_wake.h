/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __POWER_BTN_WAKE_H__
#define __POWER_BTN_WAKE_H__

typedef void (*some_handler_t)(int level);

void configure_buttons(some_handler_t callback);

#endif /* __POWER_BTN_WAKE_H__ */
