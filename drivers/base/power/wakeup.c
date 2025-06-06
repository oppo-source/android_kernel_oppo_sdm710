/*
 * drivers/base/power/wakeup.c - System wakeup events framework
 *
 * Copyright (c) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/pm_wakeirq.h>
#include <linux/types.h>
#include <trace/events/power.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>

#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
#include "../../drivers/soc/oplus/owakelock/oplus_wakelock_profiler_qcom.h"
#endif /* OPLUS_FEATURE_POWERINFO_STANDBY */

#include "power.h"

#ifdef VENDOR_EDIT
#include <linux/kobject.h>
#include <linux/sysfs.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
#include <soc/oppo/oppo_project.h>

//#define WAKEUP_SOURCE_MODEM 					60	//qcom,glink-smem-native-xprt-modem
//#define WAKEUP_SOURCE_MODEM_IPA					119 //ipa
//#define WAKEUP_SOURCE_ADSP						61  //qcom,glink-smem-native-xprt-adsp
//#define WAKEUP_SOURCE_CDSP						62	//qcom,glink-smem-native-xprt-cdsp

#define WAKEUP_SOURCE_KPDPWR 						69	//qpnp_kpdpwr_status
#define WAKEUP_SOURCE_PMIC_ALARM					595 //qpnp_rtc_alarm
#define WAKEUP_SOURCE_KPDPWR_710 					68	//qpnp_kpdpwr_status
#define WAKEUP_SOURCE_PMIC_ALARM_710				593 //qpnp_rtc_alarm
#define WAKEUP_SOURCE_KPDPWR_710P 					89	//qpnp_kpdpwr_status
#define WAKEUP_SOURCE_PMIC_ALARM_710P				600 //qpnp_rtc_alarm


u64 wakeup_source_count_kpdpwr = 0;
u64 wakeup_source_count_cdsp= 0;
u64 wakeup_source_count_adsp= 0;
u64 alarm_count = 0;
u64	wakeup_source_count_rtc = 0;
u64	wakeup_source_count_pmic_rtc= 0;
u64 wakeup_source_count_wifi = 0;

#define MODEM_WAKEUP_SRC_NUM 3
#define MODEM_DIAG_WS_INDEX 0
#define MODEM_IPA_WS_INDEX 1
#define MODEM_QMI_WS_INDEX 2

u64	wakeup_source_count_modem= 0;

int modem_wakeup_src_count[MODEM_WAKEUP_SRC_NUM] = { 0 };
char modem_wakeup_src_string[MODEM_WAKEUP_SRC_NUM][10] =
		{"DIAG_WS",
		"IPA_WS",
		"QMI_WS"};
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
static unsigned int qpnp_rtc_sirq = 0;
static unsigned int qpnp_kpdpwr_sirq = 0;
#define QPNP_RTC_IRQ_NAME   "qpnp_rtc_alarm"
#define QPNP_KPDPWR_IRQ_NAME   "qpnp_kpdpwr_status"
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
static atomic_t ws_all_release_flag = ATOMIC_INIT(1);
static ktime_t ws_start_node;
static ktime_t ws_end_node;
static ktime_t ws_hold_all_time;
static ktime_t reset_time;
static spinlock_t statistics_lock;
#endif /* VENDOR_EDIT */

#include <linux/proc_fs.h>

/*
 * If set, the suspend/hibernate code will abort transitions to a sleep state
 * if wakeup events are registered during or immediately before the transition.
 */
bool events_check_enabled __read_mostly;

/* First wakeup IRQ seen by the kernel in the last cycle. */
unsigned int pm_wakeup_irq __read_mostly;

/* If set and the system is suspending, terminate the suspend. */
static bool pm_abort_suspend __read_mostly;

/*
 * Combined counters of registered wakeup events and wakeup events in progress.
 * They need to be modified together atomically, so it's better to use one
 * atomic variable to hold them both.
 */
static atomic_t combined_event_count = ATOMIC_INIT(0);

#define IN_PROGRESS_BITS	(sizeof(int) * 4)
#define MAX_IN_PROGRESS		((1 << IN_PROGRESS_BITS) - 1)

static void split_counters(unsigned int *cnt, unsigned int *inpr)
{
	unsigned int comb = atomic_read(&combined_event_count);

	*cnt = (comb >> IN_PROGRESS_BITS);
	*inpr = comb & MAX_IN_PROGRESS;
}

/* A preserved old value of the events counter. */
static unsigned int saved_count;

static DEFINE_SPINLOCK(events_lock);

static void pm_wakeup_timer_fn(unsigned long data);

static LIST_HEAD(wakeup_sources);

static DECLARE_WAIT_QUEUE_HEAD(wakeup_count_wait_queue);

DEFINE_STATIC_SRCU(wakeup_srcu);

static struct wakeup_source deleted_ws = {
	.name = "deleted",
	.lock =  __SPIN_LOCK_UNLOCKED(deleted_ws.lock),
};

/**
 * wakeup_source_prepare - Prepare a new wakeup source for initialization.
 * @ws: Wakeup source to prepare.
 * @name: Pointer to the name of the new wakeup source.
 *
 * Callers must ensure that the @name string won't be freed when @ws is still in
 * use.
 */
void wakeup_source_prepare(struct wakeup_source *ws, const char *name)
{
	if (ws) {
		memset(ws, 0, sizeof(*ws));
		ws->name = name;
	}
}
EXPORT_SYMBOL_GPL(wakeup_source_prepare);

/**
 * wakeup_source_create - Create a struct wakeup_source object.
 * @name: Name of the new wakeup source.
 */
struct wakeup_source *wakeup_source_create(const char *name)
{
	struct wakeup_source *ws;

	ws = kmalloc(sizeof(*ws), GFP_KERNEL);
	if (!ws)
		return NULL;

	wakeup_source_prepare(ws, name ? kstrdup_const(name, GFP_KERNEL) : NULL);
	return ws;
}
EXPORT_SYMBOL_GPL(wakeup_source_create);

/**
 * wakeup_source_drop - Prepare a struct wakeup_source object for destruction.
 * @ws: Wakeup source to prepare for destruction.
 *
 * Callers must ensure that __pm_stay_awake() or __pm_wakeup_event() will never
 * be run in parallel with this function for the same wakeup source object.
 */
void wakeup_source_drop(struct wakeup_source *ws)
{
	if (!ws)
		return;

	__pm_relax(ws);
}
EXPORT_SYMBOL_GPL(wakeup_source_drop);

/*
 * Record wakeup_source statistics being deleted into a dummy wakeup_source.
 */
static void wakeup_source_record(struct wakeup_source *ws)
{
	unsigned long flags;

	spin_lock_irqsave(&deleted_ws.lock, flags);

	if (ws->event_count) {
		deleted_ws.total_time =
			ktime_add(deleted_ws.total_time, ws->total_time);
		deleted_ws.prevent_sleep_time =
			ktime_add(deleted_ws.prevent_sleep_time,
				  ws->prevent_sleep_time);
		deleted_ws.max_time =
			ktime_compare(deleted_ws.max_time, ws->max_time) > 0 ?
				deleted_ws.max_time : ws->max_time;
		deleted_ws.event_count += ws->event_count;
		deleted_ws.active_count += ws->active_count;
		deleted_ws.relax_count += ws->relax_count;
		deleted_ws.expire_count += ws->expire_count;
		deleted_ws.wakeup_count += ws->wakeup_count;
	}

	spin_unlock_irqrestore(&deleted_ws.lock, flags);
}

/**
 * wakeup_source_destroy - Destroy a struct wakeup_source object.
 * @ws: Wakeup source to destroy.
 *
 * Use only for wakeup source objects created with wakeup_source_create().
 */
void wakeup_source_destroy(struct wakeup_source *ws)
{
	if (!ws)
		return;

	wakeup_source_drop(ws);
	wakeup_source_record(ws);
	kfree_const(ws->name);
	kfree(ws);
}
EXPORT_SYMBOL_GPL(wakeup_source_destroy);

/**
 * wakeup_source_add - Add given object to the list of wakeup sources.
 * @ws: Wakeup source object to add to the list.
 */
void wakeup_source_add(struct wakeup_source *ws)
{
	unsigned long flags;

	if (WARN_ON(!ws))
		return;

	spin_lock_init(&ws->lock);
	setup_timer(&ws->timer, pm_wakeup_timer_fn, (unsigned long)ws);
	ws->active = false;
	ws->last_time = ktime_get();

	spin_lock_irqsave(&events_lock, flags);
	list_add_rcu(&ws->entry, &wakeup_sources);
	spin_unlock_irqrestore(&events_lock, flags);
}
EXPORT_SYMBOL_GPL(wakeup_source_add);

/**
 * wakeup_source_remove - Remove given object from the wakeup sources list.
 * @ws: Wakeup source object to remove from the list.
 */
void wakeup_source_remove(struct wakeup_source *ws)
{
	unsigned long flags;

	if (WARN_ON(!ws))
		return;

	spin_lock_irqsave(&events_lock, flags);
	list_del_rcu(&ws->entry);
	spin_unlock_irqrestore(&events_lock, flags);
	synchronize_srcu(&wakeup_srcu);

	del_timer_sync(&ws->timer);
	/*
	 * Clear timer.function to make wakeup_source_not_registered() treat
	 * this wakeup source as not registered.
	 */
	ws->timer.function = NULL;
}
EXPORT_SYMBOL_GPL(wakeup_source_remove);

/**
 * wakeup_source_register - Create wakeup source and add it to the list.
 * @name: Name of the wakeup source to register.
 */
struct wakeup_source *wakeup_source_register(const char *name)
{
	struct wakeup_source *ws;

	ws = wakeup_source_create(name);
	if (ws)
		wakeup_source_add(ws);

	return ws;
}
EXPORT_SYMBOL_GPL(wakeup_source_register);

/**
 * wakeup_source_unregister - Remove wakeup source from the list and remove it.
 * @ws: Wakeup source object to unregister.
 */
void wakeup_source_unregister(struct wakeup_source *ws)
{
	if (ws) {
		wakeup_source_remove(ws);
		wakeup_source_destroy(ws);
	}
}
EXPORT_SYMBOL_GPL(wakeup_source_unregister);

/**
 * device_wakeup_attach - Attach a wakeup source object to a device object.
 * @dev: Device to handle.
 * @ws: Wakeup source object to attach to @dev.
 *
 * This causes @dev to be treated as a wakeup device.
 */
static int device_wakeup_attach(struct device *dev, struct wakeup_source *ws)
{
	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		spin_unlock_irq(&dev->power.lock);
		return -EEXIST;
	}
	dev->power.wakeup = ws;
	if (dev->power.wakeirq)
		device_wakeup_attach_irq(dev, dev->power.wakeirq);
	spin_unlock_irq(&dev->power.lock);
	return 0;
}

/**
 * device_wakeup_enable - Enable given device to be a wakeup source.
 * @dev: Device to handle.
 *
 * Create a wakeup source object, register it and attach it to @dev.
 */
int device_wakeup_enable(struct device *dev)
{
	struct wakeup_source *ws;
	int ret;

	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	ws = wakeup_source_register(dev_name(dev));
	if (!ws)
		return -ENOMEM;

	ret = device_wakeup_attach(dev, ws);
	if (ret)
		wakeup_source_unregister(ws);

	return ret;
}
EXPORT_SYMBOL_GPL(device_wakeup_enable);

/**
 * device_wakeup_attach_irq - Attach a wakeirq to a wakeup source
 * @dev: Device to handle
 * @wakeirq: Device specific wakeirq entry
 *
 * Attach a device wakeirq to the wakeup source so the device
 * wake IRQ can be configured automatically for suspend and
 * resume.
 *
 * Call under the device's power.lock lock.
 */
int device_wakeup_attach_irq(struct device *dev,
			     struct wake_irq *wakeirq)
{
	struct wakeup_source *ws;

	ws = dev->power.wakeup;
	if (!ws) {
		dev_err(dev, "forgot to call call device_init_wakeup?\n");
		return -EINVAL;
	}

	if (ws->wakeirq)
		return -EEXIST;

	ws->wakeirq = wakeirq;
	return 0;
}

/**
 * device_wakeup_detach_irq - Detach a wakeirq from a wakeup source
 * @dev: Device to handle
 *
 * Removes a device wakeirq from the wakeup source.
 *
 * Call under the device's power.lock lock.
 */
void device_wakeup_detach_irq(struct device *dev)
{
	struct wakeup_source *ws;

	ws = dev->power.wakeup;
	if (ws)
		ws->wakeirq = NULL;
}

/**
 * device_wakeup_arm_wake_irqs(void)
 *
 * Itereates over the list of device wakeirqs to arm them.
 */
void device_wakeup_arm_wake_irqs(void)
{
	struct wakeup_source *ws;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry)
		dev_pm_arm_wake_irq(ws->wakeirq);
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

/**
 * device_wakeup_disarm_wake_irqs(void)
 *
 * Itereates over the list of device wakeirqs to disarm them.
 */
void device_wakeup_disarm_wake_irqs(void)
{
	struct wakeup_source *ws;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry)
		dev_pm_disarm_wake_irq(ws->wakeirq);
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

/**
 * device_wakeup_detach - Detach a device's wakeup source object from it.
 * @dev: Device to detach the wakeup source object from.
 *
 * After it returns, @dev will not be treated as a wakeup device any more.
 */
static struct wakeup_source *device_wakeup_detach(struct device *dev)
{
	struct wakeup_source *ws;

	spin_lock_irq(&dev->power.lock);
	ws = dev->power.wakeup;
	dev->power.wakeup = NULL;
	spin_unlock_irq(&dev->power.lock);
	return ws;
}

/**
 * device_wakeup_disable - Do not regard a device as a wakeup source any more.
 * @dev: Device to handle.
 *
 * Detach the @dev's wakeup source object from it, unregister this wakeup source
 * object and destroy it.
 */
int device_wakeup_disable(struct device *dev)
{
	struct wakeup_source *ws;

	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	ws = device_wakeup_detach(dev);
	wakeup_source_unregister(ws);
	return 0;
}
EXPORT_SYMBOL_GPL(device_wakeup_disable);

/**
 * device_set_wakeup_capable - Set/reset device wakeup capability flag.
 * @dev: Device to handle.
 * @capable: Whether or not @dev is capable of waking up the system from sleep.
 *
 * If @capable is set, set the @dev's power.can_wakeup flag and add its
 * wakeup-related attributes to sysfs.  Otherwise, unset the @dev's
 * power.can_wakeup flag and remove its wakeup-related attributes from sysfs.
 *
 * This function may sleep and it can't be called from any context where
 * sleeping is not allowed.
 */
void device_set_wakeup_capable(struct device *dev, bool capable)
{
	if (!!dev->power.can_wakeup == !!capable)
		return;

	if (device_is_registered(dev) && !list_empty(&dev->power.entry)) {
		if (capable) {
			if (wakeup_sysfs_add(dev))
				return;
		} else {
			wakeup_sysfs_remove(dev);
		}
	}
	dev->power.can_wakeup = capable;
}
EXPORT_SYMBOL_GPL(device_set_wakeup_capable);

/**
 * device_init_wakeup - Device wakeup initialization.
 * @dev: Device to handle.
 * @enable: Whether or not to enable @dev as a wakeup device.
 *
 * By default, most devices should leave wakeup disabled.  The exceptions are
 * devices that everyone expects to be wakeup sources: keyboards, power buttons,
 * possibly network interfaces, etc.  Also, devices that don't generate their
 * own wakeup requests but merely forward requests from one bus to another
 * (like PCI bridges) should have wakeup enabled by default.
 */
int device_init_wakeup(struct device *dev, bool enable)
{
	int ret = 0;

	if (!dev)
		return -EINVAL;

	if (enable) {
		device_set_wakeup_capable(dev, true);
		ret = device_wakeup_enable(dev);
	} else {
		if (dev->power.can_wakeup)
			device_wakeup_disable(dev);

		device_set_wakeup_capable(dev, false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(device_init_wakeup);

/**
 * device_set_wakeup_enable - Enable or disable a device to wake up the system.
 * @dev: Device to handle.
 */
int device_set_wakeup_enable(struct device *dev, bool enable)
{
	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	return enable ? device_wakeup_enable(dev) : device_wakeup_disable(dev);
}
EXPORT_SYMBOL_GPL(device_set_wakeup_enable);

/**
 * wakeup_source_not_registered - validate the given wakeup source.
 * @ws: Wakeup source to be validated.
 */
static bool wakeup_source_not_registered(struct wakeup_source *ws)
{
	/*
	 * Use timer struct to check if the given source is initialized
	 * by wakeup_source_add.
	 */
	return ws->timer.function != pm_wakeup_timer_fn ||
		   ws->timer.data != (unsigned long)ws;
}

/*
 * The functions below use the observation that each wakeup event starts a
 * period in which the system should not be suspended.  The moment this period
 * will end depends on how the wakeup event is going to be processed after being
 * detected and all of the possible cases can be divided into two distinct
 * groups.
 *
 * First, a wakeup event may be detected by the same functional unit that will
 * carry out the entire processing of it and possibly will pass it to user space
 * for further processing.  In that case the functional unit that has detected
 * the event may later "close" the "no suspend" period associated with it
 * directly as soon as it has been dealt with.  The pair of pm_stay_awake() and
 * pm_relax(), balanced with each other, is supposed to be used in such
 * situations.
 *
 * Second, a wakeup event may be detected by one functional unit and processed
 * by another one.  In that case the unit that has detected it cannot really
 * "close" the "no suspend" period associated with it, unless it knows in
 * advance what's going to happen to the event during processing.  This
 * knowledge, however, may not be available to it, so it can simply specify time
 * to wait before the system can be suspended and pass it as the second
 * argument of pm_wakeup_event().
 *
 * It is valid to call pm_relax() after pm_wakeup_event(), in which case the
 * "no suspend" period will be ended either by the pm_relax(), or by the timer
 * function executed when the timer expires, whichever comes first.
 */

/**
 * wakup_source_activate - Mark given wakeup source as active.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and, if @ws has just been activated, notify the PM
 * core of the event by incrementing the counter of of wakeup events being
 * processed.
 */
static void wakeup_source_activate(struct wakeup_source *ws)
{
	unsigned int cec;

	if (WARN_ONCE(wakeup_source_not_registered(ws),
			"unregistered wakeup source\n"))
		return;

    #ifdef OPLUS_FEATURE_POWERINFO_STANDBY
	//wakeup_get_start_hold_time();
	wakeup_get_start_time();
    #endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
	/*
	 * active wakeup source should bring the system
	 * out of PM_SUSPEND_FREEZE state
	 */
	freeze_wake();

	ws->active = true;
	ws->active_count++;
	ws->last_time = ktime_get();
	if (ws->autosleep_enabled)
		ws->start_prevent_time = ws->last_time;

	/* Increment the counter of events in progress. */
	cec = atomic_inc_return(&combined_event_count);

	trace_wakeup_source_activate(ws->name, cec);
}

/**
 * wakeup_source_report_event - Report wakeup event using the given source.
 * @ws: Wakeup source to report the event for.
 */
static void wakeup_source_report_event(struct wakeup_source *ws)
{
	ws->event_count++;
	/* This is racy, but the counter is approximate anyway. */
	if (events_check_enabled)
		ws->wakeup_count++;

	if (!ws->active)
		wakeup_source_activate(ws);
}

/**
 * __pm_stay_awake - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * It is safe to call this function from interrupt context.
 */
void __pm_stay_awake(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);

	wakeup_source_report_event(ws);
	del_timer(&ws->timer);
	ws->timer_expires = 0;

	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_stay_awake);

/**
 * pm_stay_awake - Notify the PM core that a wakeup event is being processed.
 * @dev: Device the wakeup event is related to.
 *
 * Notify the PM core of a wakeup event (signaled by @dev) by calling
 * __pm_stay_awake for the @dev's wakeup source object.
 *
 * Call this function after detecting of a wakeup event if pm_relax() is going
 * to be called directly after processing the event (and possibly passing it to
 * user space for further processing).
 */
void pm_stay_awake(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_stay_awake(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_stay_awake);

#ifdef CONFIG_PM_AUTOSLEEP
static void update_prevent_sleep_time(struct wakeup_source *ws, ktime_t now)
{
	ktime_t delta = ktime_sub(now, ws->start_prevent_time);
	ws->prevent_sleep_time = ktime_add(ws->prevent_sleep_time, delta);
}
#else
static inline void update_prevent_sleep_time(struct wakeup_source *ws,
					     ktime_t now) {}
#endif

/**
 * wakup_source_deactivate - Mark given wakeup source as inactive.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and notify the PM core that the wakeup source has
 * become inactive by decrementing the counter of wakeup events being processed
 * and incrementing the counter of registered wakeup events.
 */
static void wakeup_source_deactivate(struct wakeup_source *ws)
{
	unsigned int cnt, inpr, cec;
	ktime_t duration;
	ktime_t now;

	ws->relax_count++;
	/*
	 * __pm_relax() may be called directly or from a timer function.
	 * If it is called directly right after the timer function has been
	 * started, but before the timer function calls __pm_relax(), it is
	 * possible that __pm_stay_awake() will be called in the meantime and
	 * will set ws->active.  Then, ws->active may be cleared immediately
	 * by the __pm_relax() called from the timer function, but in such a
	 * case ws->relax_count will be different from ws->active_count.
	 */
	if (ws->relax_count != ws->active_count) {
		ws->relax_count--;
		return;
	}

	ws->active = false;

	now = ktime_get();
	duration = ktime_sub(now, ws->last_time);
	ws->total_time = ktime_add(ws->total_time, duration);
	if (ktime_to_ns(duration) > ktime_to_ns(ws->max_time))
		ws->max_time = duration;

	ws->last_time = now;
	del_timer(&ws->timer);
	ws->timer_expires = 0;

	if (ws->autosleep_enabled)
		update_prevent_sleep_time(ws, now);

	/*
	 * Increment the counter of registered wakeup events and decrement the
	 * couter of wakeup events in progress simultaneously.
	 */
	cec = atomic_add_return(MAX_IN_PROGRESS, &combined_event_count);
	trace_wakeup_source_deactivate(ws->name, cec);

	split_counters(&cnt, &inpr);
	if (!inpr && waitqueue_active(&wakeup_count_wait_queue)) {
        #ifdef OPLUS_FEATURE_POWERINFO_STANDBY
        wakeup_get_end_hold_time();
        #endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
		wake_up(&wakeup_count_wait_queue);
	}
}

/**
 * __pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * Call this function for wakeup events whose processing started with calling
 * __pm_stay_awake().
 *
 * It is safe to call it from interrupt context.
 */
void __pm_relax(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);
	if (ws->active)
		wakeup_source_deactivate(ws);
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_relax);

/**
 * pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @dev: Device that signaled the event.
 *
 * Execute __pm_relax() for the @dev's wakeup source object.
 */
void pm_relax(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_relax(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_relax);

/**
 * pm_wakeup_timer_fn - Delayed finalization of a wakeup event.
 * @data: Address of the wakeup source object associated with the event source.
 *
 * Call wakeup_source_deactivate() for the wakeup source whose address is stored
 * in @data if it is currently active and its timer has not been canceled and
 * the expiration time of the timer is not in future.
 */
static void pm_wakeup_timer_fn(unsigned long data)
{
	struct wakeup_source *ws = (struct wakeup_source *)data;
	unsigned long flags;

	spin_lock_irqsave(&ws->lock, flags);

	if (ws->active && ws->timer_expires
	    && time_after_eq(jiffies, ws->timer_expires)) {
		wakeup_source_deactivate(ws);
		ws->expire_count++;
	}

	spin_unlock_irqrestore(&ws->lock, flags);
}

/**
 * __pm_wakeup_event - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the event source.
 * @msec: Anticipated event processing time (in milliseconds).
 *
 * Notify the PM core of a wakeup event whose source is @ws that will take
 * approximately @msec milliseconds to be processed by the kernel.  If @ws is
 * not active, activate it.  If @msec is nonzero, set up the @ws' timer to
 * execute pm_wakeup_timer_fn() in future.
 *
 * It is safe to call this function from interrupt context.
 */
void __pm_wakeup_event(struct wakeup_source *ws, unsigned int msec)
{
	unsigned long flags;
	unsigned long expires;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);

	wakeup_source_report_event(ws);

	if (!msec) {
		wakeup_source_deactivate(ws);
		goto unlock;
	}

	expires = jiffies + msecs_to_jiffies(msec);
	if (!expires)
		expires = 1;

	if (!ws->timer_expires || time_after(expires, ws->timer_expires)) {
		mod_timer(&ws->timer, expires);
		ws->timer_expires = expires;
	}

 unlock:
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_wakeup_event);


/**
 * pm_wakeup_event - Notify the PM core of a wakeup event.
 * @dev: Device the wakeup event is related to.
 * @msec: Anticipated event processing time (in milliseconds).
 *
 * Call __pm_wakeup_event() for the @dev's wakeup source object.
 */
void pm_wakeup_event(struct device *dev, unsigned int msec)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_wakeup_event(dev->power.wakeup, msec);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_wakeup_event);

void pm_get_active_wakeup_sources(char *pending_wakeup_source, size_t max)
{
	struct wakeup_source *ws, *last_active_ws = NULL;
	int len = 0;
	bool active = false;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		if (ws->active && len < max) {
			if (!active)
				len += scnprintf(pending_wakeup_source, max,
						"Pending Wakeup Sources: ");
#ifndef OPLUS_FEATURE_POWERINFO_STANDBY
            len += scnprintf(pending_wakeup_source + len, max - len,
                "%s ", ws->name);
#else
            len += scnprintf(pending_wakeup_source + len, max - len,
                "%s, %ld, %ld ", ws->name, ws->active_count, ktime_to_ms(ws->total_time));
#endif
			active = true;
		} else if (!active &&
			   (!last_active_ws ||
			    ktime_to_ns(ws->last_time) >
			    ktime_to_ns(last_active_ws->last_time))) {
			last_active_ws = ws;
		}
	}
	if (!active && last_active_ws) {
#ifndef OPLUS_FEATURE_POWERINFO_STANDBY
        scnprintf(pending_wakeup_source, max,
                "Last active Wakeup Source: %s",
                last_active_ws->name);
#else
        scnprintf(pending_wakeup_source, max,
                "Last active Wakeup Source: %s, %ld, %ld",
                last_active_ws->name, last_active_ws->active_count, ktime_to_ms(last_active_ws->total_time));
#endif
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);
#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
	pr_info("%s, active: %d, pending: %s for debug\n", __func__, active, pending_wakeup_source);
#endif
}
EXPORT_SYMBOL_GPL(pm_get_active_wakeup_sources);

void pm_print_active_wakeup_sources(void)
{
	struct wakeup_source *ws;
	int srcuidx, active = 0;
	struct wakeup_source *last_activity_ws = NULL;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		if (ws->active) {
            #ifdef OPLUS_FEATURE_POWERINFO_STANDBY
            pr_info("active wakeup source: %s, %ld, %ld\n", ws->name, ws->active_count, ktime_to_ms(ws->total_time));
            #else
            pr_debug("active wakeup source: %s\n", ws->name);
            #endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
			active = 1;
		} else if (!active &&
			   (!last_activity_ws ||
			    ktime_to_ns(ws->last_time) >
			    ktime_to_ns(last_activity_ws->last_time))) {
			last_activity_ws = ws;
		}
	}

	if (!active && last_activity_ws) {
        #ifdef OPLUS_FEATURE_POWERINFO_STANDBY
        pr_info("last active wakeup source: %s, %ld, %ld\n",
            last_activity_ws->name, last_activity_ws->active_count, ktime_to_ms(last_activity_ws->total_time));
        #else
        pr_debug("last active wakeup source: %s\n",
            last_activity_ws->name);
        #endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}
EXPORT_SYMBOL_GPL(pm_print_active_wakeup_sources);


#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
void get_ws_listhead(struct list_head **ws)
{
	if (ws)
		*ws = &wakeup_sources;
}

void wakeup_srcu_read_lock(int *srcuidx)
{
	*srcuidx = srcu_read_lock(&wakeup_srcu);
}

void wakeup_srcu_read_unlock(int srcuidx)
{
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

bool ws_all_release(void)
{
	unsigned int cnt, inpr;

	pr_info("Enter: %s\n", __func__);
	split_counters(&cnt, &inpr);
	return (!inpr) ? true : false;
}
#endif /* OPLUS_FEATURE_POWERINFO_STANDBY */

/**
 * pm_wakeup_pending - Check if power transition in progress should be aborted.
 *
 * Compare the current number of registered wakeup events with its preserved
 * value from the past and return true if new wakeup events have been registered
 * since the old value was stored.  Also return true if the current number of
 * wakeup events being processed is different from zero.
 */
bool pm_wakeup_pending(void)
{
	unsigned long flags;
	bool ret = false;

	spin_lock_irqsave(&events_lock, flags);
	if (events_check_enabled) {
		unsigned int cnt, inpr;

		split_counters(&cnt, &inpr);
		ret = (cnt != saved_count || inpr > 0);
		events_check_enabled = !ret;
	}
	spin_unlock_irqrestore(&events_lock, flags);

#ifndef VENDOR_EDIT
	if (ret) {
#else
	if (ret || pm_abort_suspend) {
#endif
        #ifndef OPLUS_FEATURE_POWERINFO_STANDBY
        pr_debug("PM: Wakeup pending, aborting suspend\n");
        #else
        pr_info("PM: Wakeup pending, aborting suspend\n");
        wakeup_reasons_statics(IRQ_NAME_ABORT, WS_CNT_ABORT);
        #endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
		pm_print_active_wakeup_sources();
	}

	return ret || pm_abort_suspend;
}

void pm_system_wakeup(void)
{
	pm_abort_suspend = true;
	freeze_wake();
}
EXPORT_SYMBOL_GPL(pm_system_wakeup);

void pm_wakeup_clear(void)
{
	pm_abort_suspend = false;
	pm_wakeup_irq = 0;
}

void pm_system_irq_wakeup(unsigned int irq_number)
{
	struct irq_desc *desc;
	const char *name = "null";

	if (pm_wakeup_irq == 0) {
		if (msm_show_resume_irq_mask) {
			desc = irq_to_desc(irq_number);
			if (desc == NULL)
				name = "stray irq";
			else if (desc->action && desc->action->name)
				name = desc->action->name;

			pr_warn("%s: %d triggered %s\n", __func__,
					irq_number, name);
            #ifdef VENDOR_EDIT
			if (is_project(OPPO_18081) || is_project(OPPO_18085)) //QCM670
			{
				if(irq_number == WAKEUP_SOURCE_KPDPWR) {
					wakeup_source_count_kpdpwr++;
				}
				if(irq_number == WAKEUP_SOURCE_PMIC_ALARM) {
					wakeup_source_count_pmic_rtc++;
				}
			} else if(is_project(OPPO_18181))  //QCM710
			{
				if(irq_number == WAKEUP_SOURCE_KPDPWR_710) {
					wakeup_source_count_kpdpwr++;
				}
				if(irq_number == WAKEUP_SOURCE_PMIC_ALARM_710) {
					wakeup_source_count_pmic_rtc++;
				}
			} else {
				if (qpnp_rtc_sirq == 0 && (name != NULL) && strncmp(name, QPNP_RTC_IRQ_NAME, strlen(QPNP_RTC_IRQ_NAME)) == 0) {
					qpnp_rtc_sirq = irq_number;
				}
				if (qpnp_kpdpwr_sirq == 0 && (name != NULL) && strncmp(name, QPNP_KPDPWR_IRQ_NAME, strlen(QPNP_KPDPWR_IRQ_NAME)) == 0) {
					qpnp_kpdpwr_sirq = irq_number;
				}
			}
			#endif
			#ifdef VENDOR_EDIT
			if (irq_number == qpnp_rtc_sirq) {
				#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
	            pr_info("%s: resume caused by irq=%d, name=%s\n", __func__, irq_number, name);
	            wakeup_reasons_statics(IRQ_NAME_RTCALARM, WS_CNT_POWERKEY|WS_CNT_RTCALARM);
                #endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
			}
			if (irq_number == qpnp_kpdpwr_sirq) {
				#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
	            pr_info("%s: resume caused by irq=%d, name=%s\n", __func__, irq_number, name);
	            wakeup_reasons_statics(IRQ_NAME_POWERKEY, WS_CNT_POWERKEY|WS_CNT_RTCALARM);
                #endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
			}
			#endif /*VENDOR_EDIT*/
		}
		pm_wakeup_irq = irq_number;
		pm_system_wakeup();
	}
}

/**
 * pm_get_wakeup_count - Read the number of registered wakeup events.
 * @count: Address to store the value at.
 * @block: Whether or not to block.
 *
 * Store the number of registered wakeup events at the address in @count.  If
 * @block is set, block until the current number of wakeup events being
 * processed is zero.
 *
 * Return 'false' if the current number of wakeup events being processed is
 * nonzero.  Otherwise return 'true'.
 */
bool pm_get_wakeup_count(unsigned int *count, bool block)
{
	unsigned int cnt, inpr;

	if (block) {
		DEFINE_WAIT(wait);

		for (;;) {
			prepare_to_wait(&wakeup_count_wait_queue, &wait,
					TASK_INTERRUPTIBLE);
			split_counters(&cnt, &inpr);
			if (inpr == 0 || signal_pending(current))
				break;

			schedule();
		}
		finish_wait(&wakeup_count_wait_queue, &wait);
	}

	split_counters(&cnt, &inpr);
	*count = cnt;
	return !inpr;
}

/**
 * pm_save_wakeup_count - Save the current number of registered wakeup events.
 * @count: Value to compare with the current number of registered wakeup events.
 *
 * If @count is equal to the current number of registered wakeup events and the
 * current number of wakeup events being processed is zero, store @count as the
 * old number of registered wakeup events for pm_check_wakeup_events(), enable
 * wakeup events detection and return 'true'.  Otherwise disable wakeup events
 * detection and return 'false'.
 */
bool pm_save_wakeup_count(unsigned int count)
{
	unsigned int cnt, inpr;
	unsigned long flags;

	events_check_enabled = false;
	spin_lock_irqsave(&events_lock, flags);
	split_counters(&cnt, &inpr);
	if (cnt == count && inpr == 0) {
		saved_count = count;
		events_check_enabled = true;
	}
	spin_unlock_irqrestore(&events_lock, flags);
	return events_check_enabled;
}

#ifdef CONFIG_PM_AUTOSLEEP
/**
 * pm_wakep_autosleep_enabled - Modify autosleep_enabled for all wakeup sources.
 * @enabled: Whether to set or to clear the autosleep_enabled flags.
 */
void pm_wakep_autosleep_enabled(bool set)
{
	struct wakeup_source *ws;
	ktime_t now = ktime_get();
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		spin_lock_irq(&ws->lock);
		if (ws->autosleep_enabled != set) {
			ws->autosleep_enabled = set;
			if (ws->active) {
				if (set)
					ws->start_prevent_time = now;
				else
					update_prevent_sleep_time(ws, now);
			}
		}
		spin_unlock_irq(&ws->lock);
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}
#endif /* CONFIG_PM_AUTOSLEEP */

static struct dentry *wakeup_sources_stats_dentry;

/**
 * print_wakeup_source_stats - Print wakeup source statistics information.
 * @m: seq_file to print the statistics into.
 * @ws: Wakeup source object to print the statistics for.
 */
static int print_wakeup_source_stats(struct seq_file *m,
				     struct wakeup_source *ws)
{
	unsigned long flags;
	ktime_t total_time;
	ktime_t max_time;
	unsigned long active_count;
	ktime_t active_time;
	ktime_t prevent_sleep_time;

	spin_lock_irqsave(&ws->lock, flags);

	total_time = ws->total_time;
	max_time = ws->max_time;
	prevent_sleep_time = ws->prevent_sleep_time;
	active_count = ws->active_count;
	if (ws->active) {
		ktime_t now = ktime_get();

		active_time = ktime_sub(now, ws->last_time);
		total_time = ktime_add(total_time, active_time);
		if (active_time.tv64 > max_time.tv64)
			max_time = active_time;

		if (ws->autosleep_enabled)
			prevent_sleep_time = ktime_add(prevent_sleep_time,
				ktime_sub(now, ws->start_prevent_time));
	} else {
		active_time = ktime_set(0, 0);
	}

	seq_printf(m, "%-32s\t%lu\t\t%lu\t\t%lu\t\t%lu\t\t%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\n",
		   ws->name, active_count, ws->event_count,
		   ws->wakeup_count, ws->expire_count,
		   ktime_to_ms(active_time), ktime_to_ms(total_time),
		   ktime_to_ms(max_time), ktime_to_ms(ws->last_time),
		   ktime_to_ms(prevent_sleep_time));

	spin_unlock_irqrestore(&ws->lock, flags);

	return 0;
}

/**
 * wakeup_sources_stats_show - Print wakeup sources statistics information.
 * @m: seq_file to print the statistics into.
 */
static int wakeup_sources_stats_show(struct seq_file *m, void *unused)
{
	struct wakeup_source *ws;
	int srcuidx;

	seq_puts(m, "name\t\t\t\t\tactive_count\tevent_count\twakeup_count\t"
		"expire_count\tactive_since\ttotal_time\tmax_time\t"
		"last_change\tprevent_suspend_time\n");

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry)
		print_wakeup_source_stats(m, ws);
	srcu_read_unlock(&wakeup_srcu, srcuidx);

	print_wakeup_source_stats(m, &deleted_ws);

	return 0;
}

static int wakeup_sources_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, wakeup_sources_stats_show, NULL);
}

#ifdef VENDOR_EDIT
static ssize_t watchdog_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	s32 value;
	struct timespec ts;
	struct rtc_time tm;

	if (count == sizeof(s32)) {
		if (copy_from_user(&value, buf, sizeof(s32)))
			return -EFAULT;
	} else if (count <= 11) { /* ASCII perhaps? */
		char ascii_value[11];
		unsigned long int ulval;
		int ret;

		if (copy_from_user(ascii_value, buf, count))
			return -EFAULT;

		if (count > 10) {
			if (ascii_value[10] == '\n')
				ascii_value[10] = '\0';
			else
				return -EINVAL;
		} else {
			ascii_value[count] = '\0';
		}
		ret = kstrtoul(ascii_value, 16, &ulval);
		if (ret) {
			pr_debug("%s, 0x%lx, 0x%x\n", ascii_value, ulval, ret);
			return -EINVAL;
		}
		value = (s32)lower_32_bits(ulval);
	} else {
		return -EINVAL;
	}

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
	pr_warn("!@WatchDog_%d; %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		value, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);

	return count;
}
#endif /* VENDOR_EDIT */

static const struct file_operations wakeup_sources_stats_fops = {
	.owner = THIS_MODULE,
	.open = wakeup_sources_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
#ifdef VENDOR_EDIT
	.write          = watchdog_write,
#endif /* VENDOR_EDIT */
};

static int __init wakeup_sources_debugfs_init(void)
{
#ifndef VENDOR_EDIT
	wakeup_sources_stats_dentry = debugfs_create_file("wakeup_sources",
			S_IRUGO, NULL, NULL, &wakeup_sources_stats_fops);
#else /* VENDOR_EDIT */
	wakeup_sources_stats_dentry = debugfs_create_file("wakeup_sources",
			S_IRUGO| S_IWUGO, NULL, NULL, &wakeup_sources_stats_fops);
#endif /* VENDOR_EDIT */

    proc_create_data("wakeup_sources", 0444, NULL, &wakeup_sources_stats_fops, NULL);
    
	return 0;
}

postcore_initcall(wakeup_sources_debugfs_init);
