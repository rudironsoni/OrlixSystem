# 0004-sched-add-arch-cond-resched-hook.patch

This patch adds an optional `arch_cond_resched()` hook to Linux's
`cond_resched()` macro. The default hook is a no-op, so upstream scheduler
policy and non-Orlix behavior are unchanged.

Orlix app-hosted execution does not receive real asynchronous in-kernel timer
interrupts from iOS. The Orlix clockevent is therefore advanced by the port at
owned transition points. Long upstream kernel loops such as pipe, splice, and
zero-page I/O already call `cond_resched()` as their Linux-native cooperative
reschedule point, but they can run without returning through the Orlix syscall
or idle paths where the hosted timer is otherwise polled.

The hook cannot live entirely in the Orlix overlay because `cond_resched()` is
defined in `include/linux/sched.h`, and upstream Linux has no existing arch
callback at that exact point. The Orlix overlay provides the hook implementation
by defining `arch_cond_resched()` in `arch/orlix/include/asm/processor.h`; the
upstream-path patch only introduces the optional extension point.
