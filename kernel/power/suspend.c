/*
 * kernel/power/suspend.c - Suspend to RAM and standby functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/syscalls.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/ftrace.h>
#include <linux/rtc.h>
#include <trace/events/power.h>
#include <linux/compiler.h>
#include <linux/moduleparam.h>
#include <linux/wakeup_reason.h>

#include "power.h"
#ifdef VENDOR_EDIT
#include <linux/gpio.h>
extern int slst_gpio_base_id;
#define PROC_AWAKE_ID 12 /* 12th bit */
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
#include <soc/oppo/oppo_project.h>
__attribute__((weak)) int check_touchirq_triggered(void) {return 0;}
#endif /* VENDOR_EDIT */

const char *pm_labels[] = { "mem", "standby", "freeze", NULL };
const char *pm_states[PM_SUSPEND_MAX];

unsigned int pm_suspend_global_flags;
EXPORT_SYMBOL_GPL(pm_suspend_global_flags);

static const struct platform_suspend_ops *suspend_ops;
static const struct platform_freeze_ops *freeze_ops;
static DECLARE_WAIT_QUEUE_HEAD(suspend_freeze_wait_head);

enum freeze_state __read_mostly suspend_freeze_state;
static DEFINE_SPINLOCK(suspend_freeze_lock);

void freeze_set_ops(const struct platform_freeze_ops *ops)
{
	lock_system_sleep();
	freeze_ops = ops;
	unlock_system_sleep();
}

static void freeze_begin(void)
{
	suspend_freeze_state = FREEZE_STATE_NONE;
}

static void freeze_enter(void)
{
	spin_lock_irq(&suspend_freeze_lock);
	if (pm_wakeup_pending())
		goto out;

	suspend_freeze_state = FREEZE_STATE_ENTER;
	spin_unlock_irq(&suspend_freeze_lock);

	get_online_cpus();
	cpuidle_resume();

	/* Push all the CPUs into the idle loop. */
	wake_up_all_idle_cpus();
	pr_debug("PM: suspend-to-idle\n");
	/* Make the current CPU wait so it can enter the idle loop too. */
	wait_event(suspend_freeze_wait_head,
		   suspend_freeze_state == FREEZE_STATE_WAKE);
	pr_debug("PM: resume from suspend-to-idle\n");

	cpuidle_pause();
	put_online_cpus();

	spin_lock_irq(&suspend_freeze_lock);

 out:
	suspend_freeze_state = FREEZE_STATE_NONE;
	spin_unlock_irq(&suspend_freeze_lock);
}

void freeze_wake(void)
{
	unsigned long flags;

	spin_lock_irqsave(&suspend_freeze_lock, flags);
	if (suspend_freeze_state > FREEZE_STATE_NONE) {
		suspend_freeze_state = FREEZE_STATE_WAKE;
		wake_up(&suspend_freeze_wait_head);
	}
	spin_unlock_irqrestore(&suspend_freeze_lock, flags);
}
EXPORT_SYMBOL_GPL(freeze_wake);

static bool valid_state(suspend_state_t state)
{
	/*
	 * PM_SUSPEND_STANDBY and PM_SUSPEND_MEM states need low level
	 * support and need to be valid to the low level
	 * implementation, no valid callback implies that none are valid.
	 */
	return suspend_ops && suspend_ops->valid && suspend_ops->valid(state);
}

/*
 * If this is set, the "mem" label always corresponds to the deepest sleep state
 * available, the "standby" label corresponds to the second deepest sleep state
 * available (if any), and the "freeze" label corresponds to the remaining
 * available sleep state (if there is one).
 */
static bool relative_states;

void __init pm_states_init(void)
{
	/*
	 * freeze state should be supported even without any suspend_ops,
	 * initialize pm_states accordingly here
	 */
	pm_states[PM_SUSPEND_FREEZE] = pm_labels[relative_states ? 0 : 2];
}

static int __init sleep_states_setup(char *str)
{
	relative_states = !strncmp(str, "1", 1);
	return 1;
}

__setup("relative_sleep_states=", sleep_states_setup);

/**
 * suspend_set_ops - Set the global suspend method table.
 * @ops: Suspend operations to use.
 */
void suspend_set_ops(const struct platform_suspend_ops *ops)
{
	suspend_state_t i;
	int j = 0;

	lock_system_sleep();

	suspend_ops = ops;
	for (i = PM_SUSPEND_MEM; i >= PM_SUSPEND_STANDBY; i--)
		if (valid_state(i)) {
			pm_states[i] = pm_labels[j++];
		} else if (!relative_states) {
			pm_states[i] = NULL;
			j++;
		}

	pm_states[PM_SUSPEND_FREEZE] = pm_labels[j];

	unlock_system_sleep();
}
EXPORT_SYMBOL_GPL(suspend_set_ops);

/**
 * suspend_valid_only_mem - Generic memory-only valid callback.
 *
 * Platform drivers that implement mem suspend only and only need to check for
 * that in their .valid() callback can use this instead of rolling their own
 * .valid() callback.
 */
int suspend_valid_only_mem(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}
EXPORT_SYMBOL_GPL(suspend_valid_only_mem);

static bool sleep_state_supported(suspend_state_t state)
{
	return state == PM_SUSPEND_FREEZE || (suspend_ops && suspend_ops->enter);
}

static int platform_suspend_prepare(suspend_state_t state)
{
	return state != PM_SUSPEND_FREEZE && suspend_ops->prepare ?
		suspend_ops->prepare() : 0;
}

static int platform_suspend_prepare_late(suspend_state_t state)
{
	return state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->prepare ?
		freeze_ops->prepare() : 0;
}

static int platform_suspend_prepare_noirq(suspend_state_t state)
{
	return state != PM_SUSPEND_FREEZE && suspend_ops->prepare_late ?
		suspend_ops->prepare_late() : 0;
}

static void platform_resume_noirq(suspend_state_t state)
{
	if (state != PM_SUSPEND_FREEZE && suspend_ops->wake)
		suspend_ops->wake();
}

static void platform_resume_early(suspend_state_t state)
{
	if (state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->restore)
		freeze_ops->restore();
}

static void platform_resume_finish(suspend_state_t state)
{
	if (state != PM_SUSPEND_FREEZE && suspend_ops->finish)
		suspend_ops->finish();
}

static int platform_suspend_begin(suspend_state_t state)
{
	if (state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->begin)
		return freeze_ops->begin();
	else if (suspend_ops && suspend_ops->begin)
		return suspend_ops->begin(state);
	else
		return 0;
}

static void platform_resume_end(suspend_state_t state)
{
	if (state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->end)
		freeze_ops->end();
	else if (suspend_ops && suspend_ops->end)
		suspend_ops->end();
}

static void platform_recover(suspend_state_t state)
{
	if (state != PM_SUSPEND_FREEZE && suspend_ops->recover)
		suspend_ops->recover();
}

static bool platform_suspend_again(suspend_state_t state)
{
	return state != PM_SUSPEND_FREEZE && suspend_ops->suspend_again ?
		suspend_ops->suspend_again() : false;
}

#ifdef CONFIG_PM_DEBUG
static unsigned int pm_test_delay = 5;
module_param(pm_test_delay, uint, 0644);
MODULE_PARM_DESC(pm_test_delay,
		 "Number of seconds to wait before resuming from suspend test");
#endif

static int suspend_test(int level)
{
#ifdef CONFIG_PM_DEBUG
#ifdef VENDOR_EDIT
    pr_info("%s pm_test_level:%d, level:%d\n", __func__,
        pm_test_level, level);
#endif /* VENDOR_EDIT */
    if (pm_test_level == level) {
#ifndef VENDOR_EDIT
        pr_info("suspend debug: Waiting for %d second(s).\n",
                pm_test_delay);
#else
        pr_err("suspend debug: Waiting for %d second(s).\n",
                pm_test_delay);
#endif                
        mdelay(pm_test_delay * 1000);
        return 1;
    }
#endif /* !CONFIG_PM_DEBUG */
	return 0;
}

/**
 * suspend_prepare - Prepare for entering system sleep state.
 *
 * Common code run for every system sleep state that can be entered (except for
 * hibernation).  Run suspend notifiers, allocate the "suspend" console and
 * freeze processes.
 */
static int suspend_prepare(suspend_state_t state)
{
	int error, nr_calls = 0;

	if (!sleep_state_supported(state))
		return -EPERM;

	pm_prepare_console();

	error = __pm_notifier_call_chain(PM_SUSPEND_PREPARE, -1, &nr_calls);
	if (error) {
		nr_calls--;
		goto Finish;
	}

	trace_suspend_resume(TPS("freeze_processes"), 0, true);
	error = suspend_freeze_processes();
	trace_suspend_resume(TPS("freeze_processes"), 0, false);
	if (!error)
		return 0;

	suspend_stats.failed_freeze++;
	dpm_save_failed_step(SUSPEND_FREEZE);
 Finish:
	__pm_notifier_call_chain(PM_POST_SUSPEND, nr_calls, NULL);
	pm_restore_console();
	return error;
}

/* default implementation */
void __weak arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __weak arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}

#ifdef VENDOR_EDIT
extern void rpmstats_print(bool suspend);
extern void rpm_master_stats_print(void);
extern bool is_not_in_aosd_mode(void);
#endif
/* VENDOR_EDIT */
/**
 * suspend_enter - Make the system enter the given sleep state.
 * @state: System sleep state to enter.
 * @wakeup: Returns information that the sleep state should not be re-entered.
 *
 * This function should be called after devices have been suspended.
 */
static int suspend_enter(suspend_state_t state, bool *wakeup)
{
	char suspend_abort[MAX_SUSPEND_ABORT_LEN];
	int error, last_dev;

    error = platform_suspend_prepare(state);
#ifndef VENDOR_EDIT
    if (error)
        goto Platform_finish;
#else
    if (error) {
        pr_info("%s platform_suspend_prepare fail\n", __func__);
        goto Platform_finish;
    }
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
	rpmstats_print(true);
	rpm_master_stats_print();
	if(is_not_in_aosd_mode()){
		pr_err("[RM_POWER]: warning!!! system can not enter vddmin mode.\n");
	}
#endif
/* VENDOR_EDIT */
	error = dpm_suspend_late(PMSG_SUSPEND);
	if (error) {
		last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
		last_dev %= REC_FAILED_NUM;
		pr_err("PM: late suspend of devices failed\n");
		log_suspend_abort_reason("%s device failed to power down",
			suspend_stats.failed_devs[last_dev]);
		goto Platform_finish;
	}
	error = platform_suspend_prepare_late(state);
#ifndef VENDOR_EDIT
    if (error)
        goto Devices_early_resume;
#else
    if (error) {
        pr_info("%s prepare late fail\n", __func__);
        goto Devices_early_resume;
    }
#endif /* VENDOR_EDIT */

	error = dpm_suspend_noirq(PMSG_SUSPEND);
	if (error) {
		last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
		last_dev %= REC_FAILED_NUM;
		pr_err("PM: noirq suspend of devices failed\n");
		log_suspend_abort_reason("noirq suspend of %s device failed",
			suspend_stats.failed_devs[last_dev]);
		goto Platform_early_resume;
	}
	error = platform_suspend_prepare_noirq(state);
#ifndef VENDOR_EDIT
    if (error)
        goto Platform_wake;
#else
    if (error) {
        pr_info("%s prepare_noirq fail\n", __func__);
        goto Platform_wake;
    }
#endif /* VENDOR_EDIT */

#ifndef VENDOR_EDIT
    if (suspend_test(TEST_PLATFORM))
        goto Platform_wake;
#else
    if (suspend_test(TEST_PLATFORM)) {
        pr_info("%s test_platform fail\n", __func__);
        goto Platform_wake;
    }
#endif /* VENDOR_EDIT */
	/*
	 * PM_SUSPEND_FREEZE equals
	 * frozen processes + suspended devices + idle processors.
	 * Thus we should invoke freeze_enter() soon after
	 * all the devices are suspended.
	 */
	if (state == PM_SUSPEND_FREEZE) {
		trace_suspend_resume(TPS("machine_suspend"), state, true);
		freeze_enter();
		trace_suspend_resume(TPS("machine_suspend"), state, false);
#ifdef VENDOR_EDIT
        pr_info("%s pm_suspend_freeze\n", __func__);
#endif /* VENDOR_EDIT */
        goto Platform_wake;
	}

	error = disable_nonboot_cpus();
	if (error || suspend_test(TEST_CPUS)) {
		log_suspend_abort_reason("Disabling non-boot cpus failed");
		goto Enable_cpus;
	}

	arch_suspend_disable_irqs();
	BUG_ON(!irqs_disabled());
#ifdef VENDOR_EDIT
    pr_info("%s syscore_suspend\n", __func__);
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
    if (check_touchirq_triggered()) {
        error = -EBUSY;
        goto Enable_irqs;
    }
#endif /* VENDOR_EDIT */

	error = syscore_suspend();
	if (!error) {
		*wakeup = pm_wakeup_pending();
		if (!(suspend_test(TEST_CORE) || *wakeup)) {
			trace_suspend_resume(TPS("machine_suspend"),
				state, true);
			error = suspend_ops->enter(state);
			trace_suspend_resume(TPS("machine_suspend"),
				state, false);
			events_check_enabled = false;
		} else if (*wakeup) {
			pm_get_active_wakeup_sources(suspend_abort,
				MAX_SUSPEND_ABORT_LEN);
			log_suspend_abort_reason(suspend_abort);
			error = -EBUSY;
		}
		syscore_resume();
	}

#ifdef VENDOR_EDIT
 Enable_irqs:
#endif /* VENDOR_EDIT */
	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());

 Enable_cpus:
	enable_nonboot_cpus();

 Platform_wake:
	platform_resume_noirq(state);
	dpm_resume_noirq(PMSG_RESUME);

 Platform_early_resume:
	platform_resume_early(state);

 Devices_early_resume:
	dpm_resume_early(PMSG_RESUME);

 Platform_finish:
	platform_resume_finish(state);
	return error;
}

/**
 * suspend_devices_and_enter - Suspend devices and enter system sleep state.
 * @state: System sleep state to enter.
 */
int suspend_devices_and_enter(suspend_state_t state)
{
	int error;
	bool wakeup = false;

#ifndef VENDOR_EDIT
    if (!sleep_state_supported(state))
        return -ENOSYS;
#else
    if (!sleep_state_supported(state)) {
        pr_info("sleep_state_supported false\n");
        return -ENOSYS;
    }
#endif /* VENDOR_EDIT */

	error = platform_suspend_begin(state);
#ifndef VENDOR_EDIT
    if (error)
        goto Close;
#else
    if (error) {
        pr_info("%s platform_suspend_begin fail\n", __func__);
        goto Close;
    }
#endif /* VENDOR_EDIT */
	suspend_console();
	suspend_test_start();
	error = dpm_suspend_start(PMSG_SUSPEND);
	if (error) {
		pr_err("PM: Some devices failed to suspend, or early wake event detected\n");
		log_suspend_abort_reason("Some devices failed to suspend, or early wake event detected");
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
#ifndef VENDOR_EDIT
    if (suspend_test(TEST_DEVICES))
        goto Recover_platform;
#else
    if (suspend_test(TEST_DEVICES)) {
        pr_info("%s TEST_DEVICES fail\n", __func__);
        goto Recover_platform;
    }
#endif /* VENDOR_EDIT */
	do {
		error = suspend_enter(state, &wakeup);
	} while (!error && !wakeup && platform_suspend_again(state));

#ifdef VENDOR_EDIT
    pr_info("suspend_enter end, error:%d, wakeup:%d\n", error, wakeup);
#endif /* VENDOR_EDIT */
 Resume_devices:
	suspend_test_start();
	dpm_resume_end(PMSG_RESUME);
	suspend_test_finish("resume devices");
	trace_suspend_resume(TPS("resume_console"), state, true);
	resume_console();
	trace_suspend_resume(TPS("resume_console"), state, false);

 Close:
	platform_resume_end(state);
	return error;

 Recover_platform:
	platform_recover(state);
	goto Resume_devices;
}

/**
 * suspend_finish - Clean up before finishing the suspend sequence.
 *
 * Call platform code to clean up, restart processes, and free the console that
 * we've allocated. This routine is not called for hibernation.
 */
static void suspend_finish(void)
{
	suspend_thaw_processes();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}

#ifdef VENDOR_EDIT
/**
* Sync the filesystem in seperate workqueue.
* Then check it finishing or not periodically and
* abort if any wakeup source comes in. That can reduce
* the wakeup latency
*
*/
static bool sys_sync_completed = false;
static void sys_sync_work_func(struct work_struct *work);
static DECLARE_WORK(sys_sync_work, sys_sync_work_func);
static DECLARE_WAIT_QUEUE_HEAD(sys_sync_wait);
static void sys_sync_work_func(struct work_struct *work)
{
    trace_suspend_resume(TPS("sync_filesystems"), 0, true);
    pr_info(KERN_INFO "PM: Syncing filesystems ... ");
    sys_sync();
    pr_cont("done.\n");
    trace_suspend_resume(TPS("sync_filesystems"), 0, false);
    sys_sync_completed = true;
    wake_up(&sys_sync_wait);
}

static int sys_sync_queue(void)
{
    int work_status = work_busy(&sys_sync_work);

    /*maybe some irq coming here before pending check*/
    pm_wakeup_clear();

    /*Check if the previous work still running.*/
    if (!(work_status & WORK_BUSY_PENDING)) {
        if (work_status & WORK_BUSY_RUNNING) {
            while (wait_event_timeout(sys_sync_wait, sys_sync_completed,
                        msecs_to_jiffies(100)) == 0) {
                if (pm_wakeup_pending()) {
                    pr_info("PM: Pre-Syncing abort\n");
                    goto abort;
                }
            }
            pr_info("PM: Pre-Syncing done\n");
        }
        sys_sync_completed = false;
        schedule_work(&sys_sync_work);
    }

    while (wait_event_timeout(sys_sync_wait, sys_sync_completed,
                    msecs_to_jiffies(100)) == 0) {
        if (pm_wakeup_pending()) {
            pr_info("PM: Syncing abort\n");
            goto abort;
        }
    }

    pr_info("PM: Syncing done\n");
    return 0;
abort:
    return -EAGAIN;
}
#endif /*VENDOR_EDIT*/

/**
 * enter_state - Do common work needed to enter system sleep state.
 * @state: System sleep state to enter.
 *
 * Make sure that no one else is trying to put the system into a sleep state.
 * Fail if that's not the case.  Otherwise, prepare for system suspend, make the
 * system enter the given sleep state and clean up after wakeup.
 */
static int enter_state(suspend_state_t state)
{
	int error;

	trace_suspend_resume(TPS("suspend_enter"), state, true);
	if (state == PM_SUSPEND_FREEZE) {
#ifdef CONFIG_PM_DEBUG
		if (pm_test_level != TEST_NONE && pm_test_level <= TEST_CPUS) {
			pr_warn("PM: Unsupported test mode for suspend to idle, please choose none/freezer/devices/platform.\n");
			return -EAGAIN;
		}
#endif
	} else if (!valid_state(state)) {
#ifdef VENDOR_EDIT
        pr_info("%s invalid_state\n", __func__);
#endif /* VENDOR_EDIT */
        return -EINVAL;
    }
#ifndef VENDOR_EDIT
    if (!mutex_trylock(&pm_mutex))
        return -EBUSY;
#else
    if (!mutex_trylock(&pm_mutex)) {
        pr_info("%s mutex_trylock fail\n", __func__);
        return -EBUSY;
    }
#endif /* VENDOR_EDIT */

	if (state == PM_SUSPEND_FREEZE)
		freeze_begin();

#ifndef CONFIG_SUSPEND_SKIP_SYNC
#ifndef VENDOR_EDIT
    trace_suspend_resume(TPS("sync_filesystems"), 0, true);
    pr_info("PM: Syncing filesystems ... ");
    sys_sync();
    pr_cont("done.\n");
    trace_suspend_resume(TPS("sync_filesystems"), 0, false);
#else
    error = sys_sync_queue();
    if (error) {
        pr_err("%s sys_sync_queue fail\n", __func__);
        goto Unlock;
    }
#endif
#endif

	pr_debug("PM: Preparing system for sleep (%s)\n", pm_states[state]);
	pm_suspend_clear_flags();
	error = suspend_prepare(state);
#ifndef VENDOR_EDIT
    if (error)
        goto Unlock;
#else
    if (error) {
        pr_info("%s suspend_prepare error:%d\n", __func__, error);
        goto Unlock;
    }
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
    pr_info("%s suspend_prepare success\n", __func__);
#endif /* VENDOR_EDIT */
    if (suspend_test(TEST_FREEZER))
        goto Finish;

	trace_suspend_resume(TPS("suspend_enter"), state, false);
	pr_debug("PM: Suspending system (%s)\n", pm_states[state]);
	pm_restrict_gfp_mask();
	error = suspend_devices_and_enter(state);
	pm_restore_gfp_mask();
#ifdef VENDOR_EDIT
    pr_info("%s suspend_devices_and_enter end\n", __func__);
#endif /* VENDOR_EDIT */

 Finish:
	pr_debug("PM: Finishing wakeup.\n");
	suspend_finish();
 Unlock:
	mutex_unlock(&pm_mutex);
	return error;
}

static void pm_suspend_marker(char *annotation)
{
	struct timespec ts;
	struct rtc_time tm;

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
#ifndef VENDOR_EDIT
    pr_info("PM: suspend %s %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
        annotation, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
#else
    pr_err("PM: suspend %s %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
        annotation, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
#endif /* VENDOR_EDIT */
}

/**
 * pm_suspend - Externally visible function for suspending the system.
 * @state: System sleep state to enter.
 *
 * Check if the value of @state represents one of the supported states,
 * execute enter_state() and update system suspend statistics.
 */
int pm_suspend(suspend_state_t state)
{
	int error;

	if (state <= PM_SUSPEND_ON || state >= PM_SUSPEND_MAX)
		return -EINVAL;

	pm_suspend_marker("entry");
#ifdef VENDOR_EDIT
    gpio_set_value(slst_gpio_base_id + PROC_AWAKE_ID, 0);
    pr_err("notify adsp suspend in the beging of pm suspend before file system.\n");
#endif /* VENDOR_EDIT */
	error = enter_state(state);
#ifdef VENDOR_EDIT
        gpio_set_value(slst_gpio_base_id + PROC_AWAKE_ID, 1);
        pr_err("notify adsp resume in the end.\n");
#endif /* VENDOR_EDIT */
	if (error) {
		suspend_stats.fail++;
		dpm_save_failed_errno(error);
	} else {
		suspend_stats.success++;
#ifdef VENDOR_EDIT
		rpmstats_print(false);
#endif
/* VENDOR_EDIT */
	}
	pm_suspend_marker("exit");
	return error;
}
EXPORT_SYMBOL(pm_suspend);
