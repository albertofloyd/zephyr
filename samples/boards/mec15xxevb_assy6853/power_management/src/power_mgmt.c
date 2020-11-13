/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define CONFIG_PWRMGMT_DEEP_IDLE_ENTRY_DLY 100

#include <errno.h>
#include <zephyr.h>
#include <device.h>
#include <soc.h>
#include <power/power.h>
#include "power_mgmt_debug.h"
#include "power_btn_wake.h"
#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(pwrmgmt_test);

/* #define TASK_RUNNING_DURING_SLEEP */
#define SLP_STATES_SUPPORTED      2ul

/* Thread properties */
#define TASK_STACK_SIZE           1024ul
#define PRIORITY                  K_PRIO_COOP(5)
/* Sleep time should be lower than CONFIG_SYS_PM_MIN_RESIDENCY_SLEEP_1 */
#define THREAD_A_SLEEP_TIME       100ul
#define THREAD_B_SLEEP_TIME       1000ul
#define THREAD_C_SLEEP_TIME       8000ul

/* Maximum latency should be less than 500 ms */
#define MAX_EXPECTED_MS_LATENCY   500ul


/* Sleep some extra time than minimum residency */
#define DP_EXTRA_SLP_TIME         1100ul
#define LT_EXTRA_SLP_TIME         300ul

#define SEC_TO_MSEC               1000ul

static bool deep_idle;
static uint8_t deep_idle_delay;
static struct k_sem host_cs_req_lock;

K_THREAD_STACK_DEFINE(stack_a, TASK_STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_b, TASK_STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_c, TASK_STACK_SIZE);

static struct k_thread thread_a_id;
static struct k_thread thread_b_id;
#ifdef TASK_RUNNING_DURING_SLEEP
static struct k_thread thread_c_id;
#endif

struct pm_counter {
	uint8_t entry_cnt;
	uint8_t exit_cnt;
};

/* Track time elapsed */
static int64_t trigger_time;
static bool checks_enabled;
/* Track entry/exit to sleep */
struct pm_counter pm_counters[SLP_STATES_SUPPORTED];

static void pm_latency_check(void)
{
	int64_t latency;
	int secs;
	int msecs;

	latency = k_uptime_delta(&trigger_time);
	secs = (int)(latency / SEC_TO_MSEC);
	msecs = (int)(latency % SEC_TO_MSEC);
	LOG_INF("PM sleep entry latency %d.%03d seconds", secs, msecs);

	if (secs > 0) {
		LOG_WRN("Sleep entry latency is too high");
		return;
	}

	if (msecs > MAX_EXPECTED_MS_LATENCY) {
		LOG_WRN("Sleep entry latency is higher than expected");
	}
}

/* Hooks to count entry/exit */
void sys_pm_notify_power_state_entry(enum power_states state)
{
	if (!checks_enabled) {
		return;
	}

	switch (state) {
	case SYS_POWER_STATE_SLEEP_1:
		PM_LT_ENTER();
		pm_counters[0].entry_cnt++;
		break;
	case SYS_POWER_STATE_DEEP_SLEEP_1:
		PM_DP_ENTER();
		pm_counters[1].entry_cnt++;
		pm_latency_check();
		break;
	default:
		break;
	}
}

void sys_pm_notify_power_state_exit(enum power_states state)
{
	if (!checks_enabled) {
		return;
	}

	switch (state) {
	case SYS_POWER_STATE_SLEEP_1:
		PM_LT_EXIT();
		pm_counters[0].exit_cnt++;
		break;
	case SYS_POWER_STATE_DEEP_SLEEP_1:
		PM_DP_EXIT();
		printk("dp exit");
		pm_counters[1].exit_cnt++;
		break;
	default:
		break;
	}
}

static void pm_check_counters(uint8_t cycles)
{
	for (int i = 0; i < SLP_STATES_SUPPORTED; i++) {
		LOG_INF("PM state[%d] entry counter %d", i,
			pm_counters[i].entry_cnt);
		LOG_INF("PM state[%d] exit counter %d", i,
			pm_counters[i].exit_cnt);

		if (pm_counters[i].entry_cnt != pm_counters[i].exit_cnt) {
			LOG_WRN("PM counters entry/exit mismatch");
		}

		if (pm_counters[i].entry_cnt != cycles) {
			LOG_WRN("PM counter mismatch expected: %d", cycles);
		}

		pm_counters[i].entry_cnt = 0;
		pm_counters[i].exit_cnt = 0;
	}
}

static void pm_reset_counters(void)
{
	for (int i = 0; i < SLP_STATES_SUPPORTED; i++) {
		pm_counters[i].entry_cnt = 0;
		pm_counters[i].exit_cnt = 0;
	}

	checks_enabled = false;
	PM_TRIGGER_EXIT();
}

static void pm_trigger_marker(void)
{
	trigger_time = k_uptime_get();

	/* Directly access a pin to mark sleep trigger */
	PM_TRIGGER_ENTER();
	printk("PM >\n");
}

static void pm_exit_marker(void)
{
	int64_t residency_delta;
	int secs;
	int msecs;

	/* Directly access a pin */
	PM_TRIGGER_EXIT();
	printk("PM <\n");

	if (trigger_time > 0) {
		residency_delta = k_uptime_delta(&trigger_time);
		secs = (int)(residency_delta / SEC_TO_MSEC);
		msecs = (int)(residency_delta % SEC_TO_MSEC);
		LOG_INF("PM sleep residency %d.%03d seconds", secs, msecs);
	}
}

static int task_a_init(void)
{
	LOG_INF("Thread task A init");

	return 0;
}

static int task_b_init(void)
{
	printk("Thread task B init");

	return 0;
}

#ifdef TASK_RUNNING_DURING_SLEEP
static int task_c_init(void)
{
	LOG_INF("Thread task C init");
	GPIO_CTRL_REGS->CTRL_0054 = 0x10240UL;

	return 0;
}
#endif

void task_a_thread(void *p1, void *p2, void *p3)
{
	while (true) {
		k_msleep(THREAD_A_SLEEP_TIME);
		printk("A");
	}
}

static void task_b_thread(void *p1, void *p2, void *p3)
{
	while (true) {
		k_msleep(THREAD_B_SLEEP_TIME);
		printk("B");
	}
}

#ifdef TASK_RUNNING_DURING_SLEEP
static void task_c_thread(void *p1, void *p2, void *p3)
{
	while (true) {
		k_sleep(K_SECONDS(8));
		LOG_WRN("SIM_PECI");
		GPIO_CTRL_REGS->CTRL_0054 ^= (1ul << 16);

	}
}
#endif


static void create_tasks(void)
{
	task_a_init();
	task_b_init();
#ifdef TASK_RUNNING_DURING_SLEEP
	task_c_init();
#endif

	k_thread_create(&thread_a_id, stack_a, TASK_STACK_SIZE, task_a_thread,
		NULL, NULL, NULL, PRIORITY,  K_INHERIT_PERMS, K_FOREVER);
	k_thread_create(&thread_b_id, stack_b, TASK_STACK_SIZE, task_b_thread,
		NULL, NULL, NULL, PRIORITY,  K_INHERIT_PERMS, K_FOREVER);
#ifdef TASK_RUNNING_DURING_SLEEP
	k_thread_create(&thread_c_id, stack_c, TASK_STACK_SIZE, task_c_thread,
		NULL, NULL, NULL, PRIORITY,  K_INHERIT_PERMS, K_FOREVER);
#endif

	k_thread_start(&thread_a_id);
	k_thread_start(&thread_b_id);
#ifdef TASK_RUNNING_DURING_SLEEP
	k_thread_start(&thread_c_id);
#endif
}

static void destroy_tasks(void)
{
	k_thread_abort(&thread_a_id);
	k_thread_abort(&thread_b_id);
#ifdef TASK_RUNNING_DURING_SLEEP
	k_thread_abort(&thread_c_id);
#endif

	k_thread_join(&thread_a_id, K_FOREVER);
	k_thread_join(&thread_b_id, K_FOREVER);
#ifdef TASK_RUNNING_DURING_SLEEP
	k_thread_join(&thread_c_id, K_FOREVER);
#endif
}

static void suspend_all_tasks(void)
{
	k_thread_suspend(&thread_a_id);
	k_thread_suspend(&thread_b_id);
}

static void resume_all_tasks(void)
{
	k_thread_resume(&thread_a_id);
	k_thread_resume(&thread_b_id);
}

static void do_enter_idle(void)
{
	/* Deep sleep cycle */
	LOG_INF("Suspend...");
	suspend_all_tasks();
	deep_idle = true;
	LOG_INF("About to enter deep sleep");

	/* GPIO toggle to measure latency for deep sleep */
	pm_trigger_marker();
	/* TODO: Replace this with Zephyr sys call for suspend when available
	 * For now, the only trigger for deep sleep is k_sleep with a value
	 * greater than minimum residency
	 */
#ifdef CONFIG_SYS_PM_MIN_RESIDENCY_DEEP_SLEEP_1
	k_msleep(CONFIG_SYS_PM_MIN_RESIDENCY_DEEP_SLEEP_1 +
		 DP_EXTRA_SLP_TIME);
#endif
}

void pwrmgmt_exit_idle(void)
{
	if (deep_idle) {
		deep_idle = false;
		LOG_INF("Resume");
		resume_all_tasks();
	}
}

void pwrmgmt_request_enter_idle(uint8_t delay)
{
	deep_idle_delay = delay;
	k_sem_give(&host_cs_req_lock);
}

bool pwrmgtm_is_idle(void)
{
	return deep_idle;
}

static bool check_idle_conditions(void)
{
	return true;
}

static void prep_deep_idle(void)
{
	/* TODO: Disable callbacks for devices are not wake sources,
	 * adjust tasks periodicity for PECI, battery charging, etc
	 */
	/* Somethign lenghty here causes problems */
}

/* Single callback to handle all button change events.
 * Buttons can be identified using container of gpio_cb structure.
 */
static void some_callback(int level)
{
	if (level == 0) {
		/* pwrmgmt_request_enter_idle(100); */
	} else {
		deep_idle = 0;
	}
}

int test_pwr_mgmt_multithread_async(bool use_logging, uint8_t cycles)
{
	uint8_t iterations = cycles;

	/* Configure button */
	configure_buttons(some_callback);

	/* Create tasks */
	create_tasks();

	LOG_WRN("PM multi-thread async sleep started cycles: %d, logging: %d",
		cycles, use_logging);

	checks_enabled = true;
	k_sem_init(&host_cs_req_lock, 0, 1);

	k_msleep(900);
	pwrmgmt_request_enter_idle(CONFIG_PWRMGMT_DEEP_IDLE_ENTRY_DLY);

	while (iterations-- > 0) {
		/* Wait until request to enter deep sleep */
		k_sem_take(&host_cs_req_lock, K_FOREVER);

		/* Once the request to enter deep idle, perform sleep
		 * periodically until flag is cleared by BIOS
		 */
		do {
			prep_deep_idle();
			if (check_idle_conditions()) {
				LOG_INF("CS conditions met");
				do_enter_idle();
			}

			/* Minimum activity prior to sleep again */
			k_busy_wait(100);
			if (use_logging) {
				LOG_INF("Wake from Deep Sleep");
			} else {
				printk("Wake from Deep Sleep\n");
			}
			pm_exit_marker();
		} while (deep_idle);

		pwrmgmt_exit_idle();
	}

	destroy_tasks();

	LOG_INF("PM multi-thread completed");
	pm_check_counters(cycles);
	pm_reset_counters();

	return 0;
}


int test_pwr_mgmt_multithread(bool use_logging, uint8_t cycles)
{
	uint8_t iterations = cycles;
	/* Ensure we can enter deep sleep when stopping threads
	 * No UART output should occurr when threads are suspended
	 * Test to verify Zephyr RTOS issue #20033
	 * https://github.com/zephyrproject-rtos/zephyr/issues/20033
	 */

	create_tasks();

	LOG_WRN("PM multi-thread test started for cycles: %d, logging: %d",
		cycles, use_logging);

	checks_enabled = true;
	while (iterations-- > 0) {

		/* Light sleep cycle */
		LOG_INF("Suspend...");
		suspend_all_tasks();
		LOG_INF("About to enter light sleep");
		k_msleep(CONFIG_SYS_PM_MIN_RESIDENCY_SLEEP_1 +
			 LT_EXTRA_SLP_TIME);
		k_busy_wait(100);

		if (use_logging) {
			LOG_INF("Wake from Light Sleep");
		} else {
			printk("Wake from Light Sleep\n");
		}

		LOG_INF("Resume");
		resume_all_tasks();

		/* Deep sleep cycle */
		LOG_INF("Suspend...");
		suspend_all_tasks();
		LOG_INF("About to enter deep sleep");

		/* GPIO toggle to measure latency for deep sleep */
		pm_trigger_marker();
		k_msleep(CONFIG_SYS_PM_MIN_RESIDENCY_DEEP_SLEEP_1 +
			 DP_EXTRA_SLP_TIME);
		k_busy_wait(100);

		if (use_logging) {
			LOG_INF("Wake from Deep Sleep");
		} else {
			printk("Wake from Deep Sleep\n");
		}

		pm_exit_marker();
		LOG_INF("Resume");
		resume_all_tasks();
	}

	destroy_tasks();

	LOG_INF("PM multi-thread completed");
	pm_check_counters(cycles);
	pm_reset_counters();

	return 0;
}

int test_pwr_mgmt_singlethread(bool use_logging, uint8_t cycles)
{
	uint8_t iterations = cycles;

	LOG_WRN("PM single-thread test started for cycles: %d, logging: %d",
		cycles, use_logging);

	checks_enabled = true;
	while (iterations-- > 0) {

		/* Trigger Light Sleep 1 state. 48MHz PLL stays on */
		LOG_INF("About to enter light sleep");
		k_msleep(CONFIG_SYS_PM_MIN_RESIDENCY_SLEEP_1 +
			 LT_EXTRA_SLP_TIME);
		k_busy_wait(100);

		if (use_logging) {
			LOG_INF("Wake from Light Sleep");
		} else {
			printk("Wake from Light Sleep\n");
		}

		/* Trigger Deep Sleep 1 state. 48MHz PLL off */
		LOG_INF("About to enter deep Sleep");

		/* GPIO toggle to measure latency */
		pm_trigger_marker();
		k_msleep(CONFIG_SYS_PM_MIN_RESIDENCY_DEEP_SLEEP_1 +
			 DP_EXTRA_SLP_TIME);
		k_busy_wait(100);

		if (use_logging) {
			LOG_INF("Wake from Deep Sleep");
		} else {
			printk("Wake from Deep Sleep\n");
		}

		pm_exit_marker();
	}

	LOG_INF("PM single-thread completed");
	pm_check_counters(cycles);
	pm_reset_counters();

	return 0;
}
