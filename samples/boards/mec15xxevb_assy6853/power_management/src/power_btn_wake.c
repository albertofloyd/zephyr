/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/gpio.h>
#include <zephyr.h>
#include <soc.h>
#include <drivers/gpio.h>
#include <logging/log.h>
#include "power_btn_wake.h"
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_DECLARE(pwrmgmt_test);

#define GPIO_DEBOUNCE_PRIORITY 13
#define GPIO_DEBOUNCE_TIME   5000
#define GPIO_DEBOUNCE_CNT    5

struct btn_info {
	char			*label;
	struct device		*dev;
	uint32_t		pin;
	bool			debouncing;
	uint8_t			deb_cnt;
	bool			prev_level;
	struct gpio_callback	gpio_cb;
	char			*name;
	some_handler_t		handler;
};

struct btn_info btn_lst[] = {
	{
	.pin = MCHP_GPIO_163,
	.debouncing = false,
	.deb_cnt = GPIO_DEBOUNCE_CNT,
	.prev_level = 1,
	.dev = NULL,
	.gpio_cb = {{0}, NULL, 0},
	.label = DT_LABEL(DT_NODELABEL(gpio_140_176)),
	.name = "CS_btn",
	},
};

/* Single callback to handle all button change events.
 * Buttons can be identified using container of gpio_cb structure.
 */
void gpio_level_change_callback(const struct device *dev,
		     struct gpio_callback *gpio_cb, uint32_t pins)
{
	int level;
	struct btn_info *info = CONTAINER_OF(gpio_cb, struct btn_info, gpio_cb);

	LOG_INF("%s level changed, starting debounce", info->name);

	level = gpio_pin_get_raw(info->dev, info->pin);
	if (level < 0) {
		LOG_ERR("Fail to read");
	} else if (info->handler) {
		LOG_INF("level %d", level);
		info->handler(level);
	}
}

void configure_buttons(some_handler_t callback)
{
	for (int i = 0; i < ARRAY_SIZE(btn_lst); i++) {
		struct btn_info *info = &btn_lst[i];

		LOG_INF("Configure %s button", info->name);

		info->dev = device_get_binding(info->label);
		info->handler = callback;

		if (!info->dev) {
			LOG_ERR("Fail to bind");
			return;
		}

		gpio_init_callback(&info->gpio_cb, gpio_level_change_callback,
				   BIT(info->pin));

		if (gpio_add_callback(info->dev, &info->gpio_cb) != 0) {
			LOG_WRN("add callback fail");
		}

		if (gpio_pin_configure(info->dev, info->pin,
				       GPIO_INPUT | GPIO_INT_EDGE_BOTH)) {
			LOG_WRN("Fail to configure button!");
		}

		gpio_pin_interrupt_configure(info->dev, info->pin,
					     GPIO_INT_EDGE_BOTH);
	}
}
