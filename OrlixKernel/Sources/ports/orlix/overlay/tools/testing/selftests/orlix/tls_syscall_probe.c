// SPDX-License-Identifier: GPL-2.0

#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static uintptr_t read_tls_register(void)
{
	uintptr_t value;

	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(value));
	return value;
}

static bool has_hosted_user_tls_shape(uintptr_t value)
{
	return value >= UINT64_C(0x600000000000) &&
		value < UINT64_C(0x700000000000);
}

int main(void)
{
	uintptr_t initial_tls;
	uintptr_t after_gettid_tls;
	uintptr_t after_getpid_tls;
	long tid;
	pid_t pid;

	orlix_test_plan(4);

	initial_tls = read_tls_register();
	tid = syscall(SYS_gettid);
	after_gettid_tls = read_tls_register();
	pid = getpid();
	after_getpid_tls = read_tls_register();

	orlix_test_result(has_hosted_user_tls_shape(initial_tls),
			  "mlibc starts with hosted userspace TLS");
	orlix_test_result(tid > 0, "gettid syscall returns a task id");
	orlix_test_result(after_gettid_tls == initial_tls,
			  "raw syscall preserves userspace TLS");
	orlix_test_result(pid > 0 && after_getpid_tls == initial_tls,
			  "libc syscall wrapper preserves userspace TLS");

	orlix_test_exit();
}
