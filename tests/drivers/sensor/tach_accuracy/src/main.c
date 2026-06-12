/*
 * Copyright (c) 2026 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tach_accuracy, LOG_LEVEL_INF);

/* Compile-time configuration validation */
#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Sample requires 'sw0' alias for button in devicetree"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(tachsim))
#error "Sample requires 'tachsim' alias for pulse generator in devicetree"
#endif

#if !DT_NODE_HAS_STATUS(DT_NODELABEL(tach0), okay)
#error "Sample requires enabled 'tach0' tachometer device in devicetree"
#endif

/* Button GPIO */
#define BUTTON_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_cb_data;

/* Tachometer pulse output GPIO */
#define TACH_SIM_NODE DT_ALIAS(tachsim)
static const struct gpio_dt_spec tach_sim = GPIO_DT_SPEC_GET(TACH_SIM_NODE, gpios);

/* Tachometer sensor device */
static const struct device *const tach_dev = DEVICE_DT_GET(DT_NODELABEL(tach0));

/* Get pulses-per-round: check devicetree first, then Microchip Kconfig */
#if DT_NODE_HAS_PROP(DT_NODELABEL(tach0), pulses_per_round)
#define PULSES_PER_REV DT_PROP(DT_NODELABEL(tach0), pulses_per_round)
#elif defined(CONFIG_TACH_XEC_EDGES)
#define PULSES_PER_REV CONFIG_TACH_XEC_EDGES
#else
#error "tach0 must define pulses-per-round property or use Microchip XEC driver (CONFIG_TACH_XEC)"
#endif

/* Ensure pulses-per-round is valid */
BUILD_ASSERT(PULSES_PER_REV > 0, "PULSES_PER_REV must be greater than zero");

/* RPM test configuration */
#define RPM_TOLERANCE_PERCENT 3  /* Acceptable RPM measurement tolerance: ±3% */

/* Timing constants */
#define USEC_PER_MINUTE 60000000UL  /* Microseconds in one minute */
#define HALF_PERIOD_DIVISOR 2        /* Divide by 2 for toggle half-period */
#define TACH_STABILIZATION_MS 500    /* Time to let tach readings stabilize */
#define TACH_POLL_INTERVAL_SEC 1     /* How often to read tachometer */

/* Percentage calculation constants */
#define PERCENT_SCALE 100            /* Percentage multiplier */
#define PERCENT_DECIMAL_SCALE 1000   /* For 1 decimal place precision (e.g., 15 = 1.5%) */

/* Thread configuration */
#define TACH_THREAD_STACK_SIZE 1024  /* Stack size - increase if overflow occurs */
#define TACH_THREAD_PRIORITY 7       /* Thread priority (cooperative) */

/* Initial test configuration */
#define INITIAL_RPM 3000             /* Starting RPM value */
#define INITIAL_RPM_INDEX 2          /* Index into rpm_levels[] for INITIAL_RPM */

/* RPM test levels - covering full range */
static const uint32_t rpm_levels[] = {
	1000,  /* Low speed */
	2000,  /* Medium-low */
	3000,  /* Medium */
	4000,  /* Medium-high */
	5000,  /* High */
	6000,  /* Very high */
	8000,  /* Maximum */
};

/* Simulation state */
static struct k_timer pulse_timer;
static bool sim_running = false;
static uint32_t current_rpm = INITIAL_RPM;
static uint8_t rpm_index = INITIAL_RPM_INDEX;

/* Statistics tracking */
static struct {
	uint32_t total_samples;
	uint32_t accurate_samples;
	uint32_t max_error;
	uint32_t min_measured;
	uint32_t max_measured;
} tach_stats;

/* Calculate pulse half-period in microseconds for target RPM */
static inline uint32_t rpm_to_period_us(uint32_t rpm)
{
	return (USEC_PER_MINUTE / rpm / PULSES_PER_REV / HALF_PERIOD_DIVISOR);
}

/* Timer expiry function - generates tachometer pulses */
static void pulse_timer_handler(struct k_timer *timer)
{
	static bool state = false;

	if (sim_running) {
		state = !state;
		gpio_pin_set_dt(&tach_sim, state);
	}
}

/* Validate tachometer reading against expected RPM */
static void validate_rpm_reading(uint16_t measured, uint16_t expected)
{
	int32_t error = abs((int32_t)measured - (int32_t)expected);
	uint32_t tolerance = (expected * RPM_TOLERANCE_PERCENT) / PERCENT_SCALE;
	uint32_t error_percent_x10 = (error * PERCENT_DECIMAL_SCALE) / expected;

	tach_stats.total_samples++;

	if (error <= tolerance) {
		tach_stats.accurate_samples++;
		LOG_INF("[PASS] RPM: %d (target: %d, error: %d/%d.%d%%)",
			measured, expected, error, 
			error_percent_x10 / 10, error_percent_x10 % 10);
	} else {
		LOG_WRN("[FAIL] RPM: %d (target: %d, error: %d/%d.%d%%)",
			measured, expected, error,
			error_percent_x10 / 10, error_percent_x10 % 10);
	}

	if (error > tach_stats.max_error) {
		tach_stats.max_error = error;
	}

	if (measured < tach_stats.min_measured || tach_stats.min_measured == 0) {
		tach_stats.min_measured = measured;
	}

	if (measured > tach_stats.max_measured) {
		tach_stats.max_measured = measured;
	}
}

/* Print test statistics */
static void print_statistics(void)
{
	if (tach_stats.total_samples > 0) {
		uint32_t accuracy = (tach_stats.accurate_samples * PERCENT_SCALE) /
				    tach_stats.total_samples;

		LOG_INF("=== Test Statistics ===");
		LOG_INF("Total samples: %d", tach_stats.total_samples);
		LOG_INF("Accurate samples: %d (%d%%)",
			tach_stats.accurate_samples, accuracy);
		LOG_INF("Max error: %d RPM", tach_stats.max_error);
		LOG_INF("RPM range: %d - %d",
			tach_stats.min_measured, tach_stats.max_measured);
		LOG_INF("======================");
	}
}

/* Button press callback - cycles through RPM levels */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
			   uint32_t pins)
{
	if (!sim_running) {
		sim_running = true;
		current_rpm = rpm_levels[rpm_index];
		uint32_t period = rpm_to_period_us(current_rpm);

		LOG_INF("");
		LOG_INF(">> Starting simulation: %d RPM (period: %d us)",
			current_rpm, period);
		k_timer_start(&pulse_timer, K_USEC(period), K_USEC(period));
	} else {
		rpm_index = (rpm_index + 1) % ARRAY_SIZE(rpm_levels);
		current_rpm = rpm_levels[rpm_index];
		uint32_t period = rpm_to_period_us(current_rpm);

		LOG_INF("");
		LOG_INF(">> Changed to: %d RPM (period: %d us)",
			current_rpm, period);
		k_timer_start(&pulse_timer, K_USEC(period), K_USEC(period));

		if (rpm_index == 0) {
			print_statistics();
		}
	}
}

/* Thread to continuously read and validate tachometer */
static void tach_read_thread(void *arg1, void *arg2, void *arg3)
{
	struct sensor_value rpm;
	int ret;

	LOG_INF("Tachometer monitoring thread started");

	/* Wait for simulation to start (button press) before attempting fetches */
	while (!sim_running) {
		k_sleep(K_MSEC(100));
	}

	/* Allow pulses to stabilize after button press */
	k_sleep(K_MSEC(TACH_STABILIZATION_MS));

	while (1) {
		ret = sensor_sample_fetch_chan(tach_dev, SENSOR_CHAN_RPM);
		if (ret) {
			LOG_ERR("Failed to fetch sample: %d", ret);
			k_sleep(K_SECONDS(TACH_POLL_INTERVAL_SEC));
			continue;
		}

		ret = sensor_channel_get(tach_dev, SENSOR_CHAN_RPM, &rpm);
		if (ret) {
			LOG_ERR("Failed to get RPM channel: %d", ret);
			k_sleep(K_SECONDS(TACH_POLL_INTERVAL_SEC));
			continue;
		}

		if (sim_running) {
			uint16_t rpm_val = (uint16_t)rpm.val1;

			validate_rpm_reading(rpm_val, current_rpm);
		} else {
			if (rpm.val1 == 0) {
				LOG_INF("RPM: 0 (idle - no simulation running)");
			} else {
				LOG_WRN("Unexpected RPM reading: %d (should be 0)",
					rpm.val1);
			}
		}

		k_sleep(K_SECONDS(TACH_POLL_INTERVAL_SEC));
	}
}

K_THREAD_DEFINE(tach_read_tid, TACH_THREAD_STACK_SIZE, tach_read_thread, 
		NULL, NULL, NULL, TACH_THREAD_PRIORITY, 0, 0);

int main(void)
{
	int ret;

	printk("\n");
	LOG_INF("================================================");
	LOG_INF("       Tachometer Accuracy Test Sample        ");
	LOG_INF("================================================");
	LOG_INF("");

	memset(&tach_stats, 0, sizeof(tach_stats));

	if (!device_is_ready(tach_dev)) {
		LOG_ERR("[ERR] Tachometer device not ready!");
		return -ENODEV;
	}
	LOG_INF("[OK] Tachometer device ready (%s)", tach_dev->name);
	LOG_INF("     Pulses per revolution: %d", PULSES_PER_REV);

	if (!device_is_ready(button.port)) {
		LOG_ERR("[ERR] Button GPIO not ready!");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("[ERR] Failed to configure button: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("[ERR] Failed to configure button interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	LOG_INF("[OK] Button configured");

	if (!device_is_ready(tach_sim.port)) {
		LOG_ERR("[ERR] Pulse generator GPIO not ready!");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&tach_sim, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("[ERR] Failed to configure pulse generator: %d", ret);
		return ret;
	}
	LOG_INF("[OK] Pulse generator configured");

	k_timer_init(&pulse_timer, pulse_timer_handler, NULL);
	LOG_INF("[OK] Pulse timer initialized");

	LOG_INF("");
	LOG_INF("=== Hardware Setup ===");
	LOG_INF("Connect pulse generator GPIO to tachometer input GPIO");
	LOG_INF("(See README for board-specific wiring details)");
	LOG_INF("");
	LOG_INF("=== Instructions ===");
	LOG_INF("1. Press button to START at %d RPM", rpm_levels[rpm_index]);
	LOG_INF("2. Press again to cycle through test speeds:");
	for (int i = 0; i < ARRAY_SIZE(rpm_levels); i++) {
		LOG_INF("   - %d RPM", rpm_levels[i]);
	}
	LOG_INF("3. Readings displayed every second");
	LOG_INF("4. Statistics shown after full cycle");
	LOG_INF("");
	LOG_INF("Waiting for button press to begin...");

	return 0;
}
