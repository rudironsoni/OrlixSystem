/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/unistd.h>

struct orlix_init_exec_probe_timespec {
	long long tv_sec;
	long long tv_nsec;
};

static inline long orlix_syscall2(long number, long arg0, long arg1)
{
	register long x8 __asm__("x8") = number;
	register long x0 __asm__("x0") = arg0;
	register long x1 __asm__("x1") = arg1;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x8)
			 : "memory", "cc");
	return x0;
}

static inline long orlix_syscall3(long number, long arg0, long arg1, long arg2)
{
	register long x8 __asm__("x8") = number;
	register long x0 __asm__("x0") = arg0;
	register long x1 __asm__("x1") = arg1;
	register long x2 __asm__("x2") = arg2;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x8)
			 : "memory", "cc");
	return x0;
}

__attribute__((noreturn)) void _start(void)
{
	static const char message[] = "ORLIX-INIT-EXEC-PROBE\n";
	static const struct orlix_init_exec_probe_timespec one_second = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};

	orlix_syscall3(__NR_write, 1, (long)message, sizeof(message) - 1);
	for (;;)
		orlix_syscall2(__NR_nanosleep, (long)&one_second, 0);
}
