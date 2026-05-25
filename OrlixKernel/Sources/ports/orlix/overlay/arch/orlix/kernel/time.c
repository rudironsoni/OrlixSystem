// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/of_clk.h>
#include <linux/sched_clock.h>
#include <linux/smp.h>
#include <linux/time64.h>
#include <asm/irq_regs.h>
#include <asm/ptrace.h>
#include <asm/time.h>
#include <internal/asm/host_time.h>

#define ORLIX_HOST_TIMER_HZ NSEC_PER_SEC
#define ORLIX_HOST_TIMER_MIN_DELTA 1
#define ORLIX_HOST_TIMER_MAX_DELTA (~0UL)

static u64 orlix_clock_read(struct clocksource *cs)
{
	(void)cs;

	return orlix_host_time_monotonic_ns();
}

static u64 notrace orlix_sched_clock_read(void)
{
	return orlix_host_time_monotonic_ns();
}

static struct clocksource orlix_clocksource = {
	.name	= "orlix-host-time",
	.rating	= 300,
	.read	= orlix_clock_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP,
};

static struct clock_event_device orlix_clockevent;
static u64 orlix_clockevent_deadline_ns;
static u64 orlix_clockevent_period_ns;
static bool orlix_clockevent_armed;
static bool orlix_clockevent_periodic;

static int orlix_timer_set_next_event(unsigned long delta,
				      struct clock_event_device *evt)
{
	(void)evt;

	WRITE_ONCE(orlix_clockevent_periodic, false);
	WRITE_ONCE(orlix_clockevent_deadline_ns,
		   orlix_host_time_monotonic_ns() + delta);
	WRITE_ONCE(orlix_clockevent_armed, true);
	return 0;
}

static int orlix_timer_set_periodic(struct clock_event_device *evt)
{
	u64 period_ns = NSEC_PER_SEC / HZ;

	(void)evt;

	WRITE_ONCE(orlix_clockevent_period_ns, period_ns);
	WRITE_ONCE(orlix_clockevent_periodic, true);
	WRITE_ONCE(orlix_clockevent_deadline_ns,
		   orlix_host_time_monotonic_ns() + period_ns);
	WRITE_ONCE(orlix_clockevent_armed, true);
	return 0;
}

static int orlix_timer_set_oneshot(struct clock_event_device *evt)
{
	(void)evt;

	WRITE_ONCE(orlix_clockevent_periodic, false);
	WRITE_ONCE(orlix_clockevent_armed, false);
	return 0;
}

static int orlix_timer_shutdown(struct clock_event_device *evt)
{
	(void)evt;

	WRITE_ONCE(orlix_clockevent_periodic, false);
	WRITE_ONCE(orlix_clockevent_armed, false);
	return 0;
}

static struct clock_event_device orlix_clockevent = {
	.name			= "orlix-host-timer",
	.features		= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 300,
	.set_next_event		= orlix_timer_set_next_event,
	.set_state_periodic	= orlix_timer_set_periodic,
	.set_state_oneshot	= orlix_timer_set_oneshot,
	.set_state_shutdown	= orlix_timer_shutdown,
	.tick_resume		= orlix_timer_shutdown,
};

unsigned long long orlix_timer_next_deadline_ns(void)
{
	if (!READ_ONCE(orlix_clockevent_armed))
		return 0;

	return READ_ONCE(orlix_clockevent_deadline_ns);
}

void orlix_timer_poll(void)
{
	struct clock_event_device *evt = &orlix_clockevent;
	struct pt_regs timer_regs = {
		.pstate = PSR_MODE_EL1h,
	};
	struct pt_regs *old_regs;
	unsigned long flags;
	u64 deadline;
	u64 now;

	if (!READ_ONCE(orlix_clockevent_armed) || !evt->event_handler)
		return;

	now = orlix_host_time_monotonic_ns();
	deadline = READ_ONCE(orlix_clockevent_deadline_ns);
	if ((s64)(now - deadline) < 0)
		return;

	if (READ_ONCE(orlix_clockevent_periodic)) {
		u64 period_ns = READ_ONCE(orlix_clockevent_period_ns);
		u64 next_deadline = deadline + period_ns;

		if ((s64)(now - next_deadline) >= 0)
			next_deadline = now + period_ns;
		WRITE_ONCE(orlix_clockevent_deadline_ns, next_deadline);
	} else {
		WRITE_ONCE(orlix_clockevent_armed, false);
	}

	local_irq_save(flags);
	old_regs = set_irq_regs(&timer_regs);
	irq_enter();
	evt->event_handler(evt);
	irq_exit();
	set_irq_regs(old_regs);
	local_irq_restore(flags);
}

static void __init orlix_timer_init(void)
{
	if (clocksource_register_hz(&orlix_clocksource, ORLIX_HOST_TIMER_HZ))
		panic("Orlix failed to register host clocksource");

	sched_clock_register(orlix_sched_clock_read, 64, ORLIX_HOST_TIMER_HZ);

	orlix_clockevent.cpumask = cpumask_of(smp_processor_id());
	clockevents_config_and_register(&orlix_clockevent, ORLIX_HOST_TIMER_HZ,
					ORLIX_HOST_TIMER_MIN_DELTA,
					ORLIX_HOST_TIMER_MAX_DELTA);
}

void __init time_init(void)
{
	orlix_timer_init();
	of_clk_init(NULL);
	timer_probe();
	lpj_fine = 1000000UL;
}
