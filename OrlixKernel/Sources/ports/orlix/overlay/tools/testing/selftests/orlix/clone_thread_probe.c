// SPDX-License-Identifier: GPL-2.0

#include <linux/sched.h>
#include <alloca.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

#define KIB 1024UL
#define MIB (1024UL * KIB)

struct clone_probe_state {
	volatile int child_started;
	volatile long child_tid;
	int parent_tid;
};

struct mlibc_join_probe_tcb {
	volatile int tid;
	volatile int did_exit;
	volatile int child_entered;
	volatile int child_returned;
	unsigned long stack_trample_size;
};

#define ORLIX_STRINGIFY_1(x) #x
#define ORLIX_STRINGIFY(x) ORLIX_STRINGIFY_1(x)

static long orlix_raw_syscall1(long nr, long arg0)
{
	register long x0 __asm__("x0") = arg0;
	register long x8 __asm__("x8") = nr;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x8)
			 : "memory", "cc");
	return x0;
}

static long orlix_raw_syscall4(long nr, long arg0, long arg1, long arg2,
			       long arg3)
{
	register long x0 __asm__("x0") = arg0;
	register long x1 __asm__("x1") = arg1;
	register long x2 __asm__("x2") = arg2;
	register long x3 __asm__("x3") = arg3;
	register long x8 __asm__("x8") = nr;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
			 : "memory", "cc");
	return x0;
}

static int orlix_futex_wait(volatile int *uaddr, int expected)
{
	return (int)orlix_raw_syscall4(SYS_futex, (long)uaddr, 0, expected, 0);
}

static int orlix_futex_wake(volatile int *uaddr)
{
	return (int)orlix_raw_syscall4(SYS_futex, (long)uaddr, 1, INT32_MAX, 0);
}

static uintptr_t orlix_read_tls_register(void)
{
	uintptr_t value;

	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(value));
	return value;
}

static void report_clone_state(const char *label, long ret,
			       const struct clone_probe_state *state)
{
	char line[160];
	size_t pos = 0;

	pos = orlix_append_cstr(line, pos, sizeof(line), "# ");
	pos = orlix_append_cstr(line, pos, sizeof(line), label);
	pos = orlix_append_cstr(line, pos, sizeof(line), " ret=");
	if (ret < 0) {
		pos = orlix_append_cstr(line, pos, sizeof(line), "-");
		pos = orlix_append_uint(line, pos, sizeof(line),
					(unsigned int)-ret);
	} else {
		pos = orlix_append_uint(line, pos, sizeof(line),
					(unsigned int)ret);
	}
	pos = orlix_append_cstr(line, pos, sizeof(line), " parent_tid=");
	pos = orlix_append_uint(line, pos, sizeof(line),
				(unsigned int)state->parent_tid);
	pos = orlix_append_cstr(line, pos, sizeof(line), " child_started=");
	pos = orlix_append_uint(line, pos, sizeof(line),
				(unsigned int)state->child_started);
	pos = orlix_append_cstr(line, pos, sizeof(line), " child_tid=");
	pos = orlix_append_uint(line, pos, sizeof(line),
				(unsigned int)state->child_tid);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\n");
	orlix_write_bytes(line, pos);
}

static void report_stack_prepare_failure(const char *operation, int error)
{
	char line[96];
	size_t pos = 0;

	pos = orlix_append_cstr(line, pos, sizeof(line), "# stack ");
	pos = orlix_append_cstr(line, pos, sizeof(line), operation);
	pos = orlix_append_cstr(line, pos, sizeof(line), " failed errno=");
	pos = orlix_append_uint(line, pos, sizeof(line), (unsigned int)error);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\n");
	orlix_write_bytes(line, pos);
}

static void clone_child_entry(void *arg)
{
	struct clone_probe_state *state = arg;

	state->child_tid = orlix_raw_syscall1(SYS_gettid, 0);
	__atomic_store_n(&state->child_started, 1, __ATOMIC_RELEASE);
	orlix_raw_syscall1(SYS_exit, 0);
	__builtin_unreachable();
}

static void mlibc_join_child_entry(void *arg)
{
	struct mlibc_join_probe_tcb *tcb = arg;

	__atomic_store_n(&tcb->child_entered, 1, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&tcb->tid, __ATOMIC_ACQUIRE))
		(void)orlix_futex_wait(&tcb->tid, 0);
	__atomic_store_n(&tcb->child_returned, 1, __ATOMIC_RELEASE);
	__atomic_store_n(&tcb->did_exit, 1, __ATOMIC_RELEASE);
	(void)orlix_futex_wake(&tcb->did_exit);
	orlix_raw_syscall1(SYS_exit, 0);
	__builtin_unreachable();
}

static void mlibc_stack_trample_child_entry(void *arg)
{
	struct mlibc_join_probe_tcb *tcb = arg;
	void *area;

	__atomic_store_n(&tcb->child_entered, 1, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&tcb->tid, __ATOMIC_ACQUIRE))
		(void)orlix_futex_wait(&tcb->tid, 0);

	area = alloca(tcb->stack_trample_size);
	*(volatile char *)area = 1;
	*(volatile char *)((char *)area + tcb->stack_trample_size - 1) = 1;

	__atomic_store_n(&tcb->child_returned, 1, __ATOMIC_RELEASE);
	__atomic_store_n(&tcb->did_exit, 1, __ATOMIC_RELEASE);
	(void)orlix_futex_wake(&tcb->did_exit);
	orlix_raw_syscall1(SYS_exit, 0);
	__builtin_unreachable();
}

void __attribute__((noinline)) clone_child_trampoline(void (*entry)(void *),
						      void *arg)
{
	entry(arg);
	orlix_raw_syscall1(SYS_exit, 0);
	__builtin_unreachable();
}

long orlix_clone_thread_syscall(unsigned long flags, void *stack,
				int *parent_tid, unsigned long tls);

__asm__(
".p2align 2\n"
"orlix_clone_thread_syscall:\n"
"	mov	x4, xzr\n"
"	mov	x8, #" ORLIX_STRINGIFY(SYS_clone) "\n"
"	svc	#0\n"
"	cbz	x0, 1f\n"
"	ret\n"
"1:\n"
"	ldp	x0, x1, [sp], #16\n"
"	bl	clone_child_trampoline\n"
"	mov	x0, xzr\n"
"	mov	x8, #" ORLIX_STRINGIFY(SYS_exit) "\n"
"	svc	#0\n"
"	brk	#0\n");

static void *prepare_mlibc_shaped_stack(size_t guard_size, size_t stack_size,
					void *entry, void *arg, void **mapping,
					size_t *mapping_size)
{
	uintptr_t *sp;

	*mapping_size = guard_size + stack_size;
	*mapping = mmap(NULL, *mapping_size, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (*mapping == MAP_FAILED) {
		report_stack_prepare_failure("mmap", errno);
		return NULL;
	}

	if (mprotect((char *)*mapping + guard_size, stack_size,
		     PROT_READ | PROT_WRITE) != 0) {
		report_stack_prepare_failure("mprotect", errno);
		(void)munmap(*mapping, *mapping_size);
		return NULL;
	}

	sp = (uintptr_t *)((char *)*mapping + guard_size + stack_size);
	sp = (uintptr_t *)((uintptr_t)sp & ~(uintptr_t)0xf);
	*--sp = (uintptr_t)arg;
	*--sp = (uintptr_t)entry;
	return sp;
}

static bool clone_thread_with_mlibc_stack_runs(void)
{
	struct clone_probe_state state = {};
	unsigned long flags = CLONE_VM | CLONE_FS | CLONE_FILES |
		CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
		CLONE_SETTLS | CLONE_PARENT_SETTID;
	void *mapping = NULL;
	size_t mapping_size = 0;
	void *stack;
	long page_size;
	long ret;
	int i;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		report_stack_prepare_failure("sysconf", errno);
		return false;
	}

	stack = prepare_mlibc_shaped_stack((size_t)page_size, 4 * MIB,
					   clone_child_entry, &state, &mapping,
					   &mapping_size);
	if (!stack)
		return false;

	ret = orlix_clone_thread_syscall(flags, stack, &state.parent_tid,
					orlix_read_tls_register());
	if (ret < 0) {
		report_clone_state("clone syscall failed", ret, &state);
		(void)munmap(mapping, mapping_size);
		return false;
	}

	for (i = 0; i < 1000; i++) {
		if (__atomic_load_n(&state.child_started, __ATOMIC_ACQUIRE))
			break;
		orlix_raw_syscall1(SYS_sched_yield, 0);
	}

	if (!(ret > 0 && state.parent_tid == ret && state.child_tid > 0))
		report_clone_state("clone thread did not complete", ret, &state);

	(void)munmap(mapping, mapping_size);
	return ret > 0 && state.parent_tid == ret && state.child_tid > 0;
}

static bool clone_thread_with_mlibc_join_handshake_completes(void)
{
	struct mlibc_join_probe_tcb *tcb;
	unsigned long flags = CLONE_VM | CLONE_FS | CLONE_FILES |
		CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
		CLONE_SETTLS | CLONE_PARENT_SETTID;
	void *mapping = NULL;
	size_t mapping_size = 0;
	void *stack;
	long page_size;
	long ret;
	int parent_tid = 0;
	int i;
	bool completed;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		report_stack_prepare_failure("sysconf", errno);
		return false;
	}

	tcb = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (tcb == MAP_FAILED) {
		report_stack_prepare_failure("tcb mmap", errno);
		return false;
	}

	stack = prepare_mlibc_shaped_stack((size_t)page_size, 4 * MIB,
					   mlibc_join_child_entry, tcb, &mapping,
					   &mapping_size);
	if (!stack) {
		(void)munmap(tcb, (size_t)page_size);
		return false;
	}

	ret = orlix_clone_thread_syscall(flags, stack, &parent_tid,
					(unsigned long)tcb);
	if (ret < 0) {
		struct clone_probe_state state = { .parent_tid = parent_tid };

		report_clone_state("mlibc join clone syscall failed", ret,
				   &state);
		(void)munmap(mapping, mapping_size);
		(void)munmap(tcb, (size_t)page_size);
		return false;
	}

	__atomic_store_n(&tcb->tid, (int)ret, __ATOMIC_RELEASE);
	(void)orlix_futex_wake(&tcb->tid);

	for (i = 0; i < 1000; i++) {
		if (__atomic_load_n(&tcb->did_exit, __ATOMIC_ACQUIRE))
			break;
		(void)orlix_futex_wait(&tcb->did_exit, 0);
	}

	completed = ret > 0 && parent_tid == ret &&
		__atomic_load_n(&tcb->child_entered, __ATOMIC_ACQUIRE) &&
		__atomic_load_n(&tcb->child_returned, __ATOMIC_ACQUIRE) &&
		__atomic_load_n(&tcb->did_exit, __ATOMIC_ACQUIRE);
	if (!completed) {
		struct clone_probe_state state = {
			.child_started = tcb->child_entered,
			.child_tid = tcb->did_exit,
			.parent_tid = parent_tid,
		};

		report_clone_state("mlibc join handshake did not complete",
				   ret, &state);
	}

	(void)munmap(mapping, mapping_size);
	(void)munmap(tcb, (size_t)page_size);
	return completed;
}

static bool clone_thread_with_mlibc_stack_trample_completes(void)
{
	struct mlibc_join_probe_tcb *tcb;
	unsigned long flags = CLONE_VM | CLONE_FS | CLONE_FILES |
		CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
		CLONE_SETTLS | CLONE_PARENT_SETTID;
	void *mapping = NULL;
	size_t mapping_size = 0;
	void *stack;
	long page_size;
	long ret;
	int parent_tid = 0;
	int i;
	bool completed;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		report_stack_prepare_failure("sysconf", errno);
		return false;
	}

	tcb = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (tcb == MAP_FAILED) {
		report_stack_prepare_failure("tcb mmap", errno);
		return false;
	}
	tcb->stack_trample_size = 3 * MIB;

	stack = prepare_mlibc_shaped_stack((size_t)page_size, 4 * MIB,
					   mlibc_stack_trample_child_entry,
					   tcb, &mapping, &mapping_size);
	if (!stack) {
		(void)munmap(tcb, (size_t)page_size);
		return false;
	}

	ret = orlix_clone_thread_syscall(flags, stack, &parent_tid,
					(unsigned long)tcb);
	if (ret < 0) {
		struct clone_probe_state state = { .parent_tid = parent_tid };

		report_clone_state("mlibc stack trample clone syscall failed",
				   ret, &state);
		(void)munmap(mapping, mapping_size);
		(void)munmap(tcb, (size_t)page_size);
		return false;
	}

	__atomic_store_n(&tcb->tid, (int)ret, __ATOMIC_RELEASE);
	(void)orlix_futex_wake(&tcb->tid);

	for (i = 0; i < 1000; i++) {
		if (__atomic_load_n(&tcb->did_exit, __ATOMIC_ACQUIRE))
			break;
		(void)orlix_futex_wait(&tcb->did_exit, 0);
	}

	completed = ret > 0 && parent_tid == ret &&
		__atomic_load_n(&tcb->child_entered, __ATOMIC_ACQUIRE) &&
		__atomic_load_n(&tcb->child_returned, __ATOMIC_ACQUIRE) &&
		__atomic_load_n(&tcb->did_exit, __ATOMIC_ACQUIRE);
	if (!completed) {
		struct clone_probe_state state = {
			.child_started = tcb->child_entered,
			.child_tid = tcb->did_exit,
			.parent_tid = parent_tid,
		};

		report_clone_state("mlibc stack trample did not complete",
				   ret, &state);
	}

	(void)munmap(mapping, mapping_size);
	(void)munmap(tcb, (size_t)page_size);
	return completed;
}

int main(void)
{
	orlix_test_plan(3);
	orlix_test_result(clone_thread_with_mlibc_stack_runs(),
			  "mlibc-shaped clone thread stack runs through Linux clone");
	orlix_test_result(clone_thread_with_mlibc_join_handshake_completes(),
			  "mlibc-shaped clone TLS and futex join handshake completes");
	orlix_test_result(clone_thread_with_mlibc_stack_trample_completes(),
			  "mlibc-shaped clone stack supports deep alloca faults");
	orlix_test_exit();
}
