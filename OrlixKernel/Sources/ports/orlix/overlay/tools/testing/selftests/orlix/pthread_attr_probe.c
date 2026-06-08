// SPDX-License-Identifier: GPL-2.0

#include <alloca.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static void probe_stage(const char *stage)
{
	orlix_write_all("# pthread_attr ");
	orlix_write_all(stage);
	orlix_write_all("\n");
}

static void *stacksize_worker(void *arg)
{
	size_t default_stacksize = *(size_t *)arg;
	size_t alloc_size = default_stacksize + default_stacksize / 2;
	void *area;

	probe_stage("stacksize child entered");
	area = alloca(alloc_size);
	*(volatile char *)area = 1;
	*(volatile char *)((char *)area + alloc_size - 1) = 1;
	probe_stage("stacksize child returning");
	return NULL;
}

static bool pthread_stacksize_create_join_completes(void)
{
	pthread_attr_t attr;
	size_t stacksize;
	pthread_t thread;

	probe_stage("stacksize attr init");
	if (pthread_attr_init(&attr))
		return false;
	if (pthread_attr_getstacksize(&attr, &stacksize))
		return false;
	probe_stage("stacksize attr setstacksize");
	if (pthread_attr_setstacksize(&attr, stacksize * 2))
		return false;
	probe_stage("stacksize create");
	if (pthread_create(&thread, &attr, stacksize_worker, &stacksize))
		return false;
	probe_stage("stacksize join");
	if (pthread_join(thread, NULL))
		return false;
	probe_stage("stacksize joined");
	return true;
}

static bool pthread_detachstate_round_trip_completes(void)
{
	pthread_attr_t attr;
	int detachstate;

	probe_stage("detachstate set detached");
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		return false;
	probe_stage("detachstate get");
	if (pthread_attr_getdetachstate(&attr, &detachstate))
		return false;
	if (detachstate != PTHREAD_CREATE_DETACHED)
		return false;
	probe_stage("detachstate invalid");
	return pthread_attr_setdetachstate(
		       &attr, 2 * (PTHREAD_CREATE_DETACHED +
				    PTHREAD_CREATE_JOINABLE)) == EINVAL;
}

static bool pthread_guardsize_round_trip_completes(void)
{
	pthread_attr_t attr;
	size_t guardsize;

	probe_stage("guardsize attr init");
	if (pthread_attr_init(&attr))
		return false;
	probe_stage("guardsize set zero");
	if (pthread_attr_setguardsize(&attr, 0))
		return false;
	probe_stage("guardsize get");
	if (pthread_attr_getguardsize(&attr, &guardsize))
		return false;
	return guardsize == 0;
}

static bool pthread_scope_round_trip_completes(void)
{
	pthread_attr_t attr;
	int scope;

	probe_stage("scope set system");
	if (pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM))
		return false;
	probe_stage("scope get");
	if (pthread_attr_getscope(&attr, &scope))
		return false;
	if (scope != PTHREAD_SCOPE_SYSTEM)
		return false;
	probe_stage("scope invalid");
	return pthread_attr_setscope(
		       &attr, 2 * (PTHREAD_SCOPE_SYSTEM +
				    PTHREAD_SCOPE_PROCESS)) == EINVAL;
}

static bool pthread_inheritsched_round_trip_completes(void)
{
	pthread_attr_t attr;
	int inheritsched;

	probe_stage("inheritsched set inherit");
	if (pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED))
		return false;
	probe_stage("inheritsched get");
	if (pthread_attr_getinheritsched(&attr, &inheritsched))
		return false;
	if (inheritsched != PTHREAD_INHERIT_SCHED)
		return false;
	probe_stage("inheritsched invalid");
	return pthread_attr_setinheritsched(
		       &attr, 2 * (PTHREAD_INHERIT_SCHED +
				    PTHREAD_EXPLICIT_SCHED)) == EINVAL;
}

static bool pthread_schedparam_round_trip_completes(void)
{
	pthread_attr_t attr;
	struct sched_param init_param = { 0 };
	struct sched_param param = { 1 };

	probe_stage("schedparam set");
	if (pthread_attr_setschedparam(&attr, &init_param))
		return false;
	probe_stage("schedparam get");
	if (pthread_attr_getschedparam(&attr, &param))
		return false;
	return param.sched_priority == init_param.sched_priority;
}

static bool pthread_schedpolicy_round_trip_completes(void)
{
	pthread_attr_t attr;
	int policy;

	probe_stage("schedpolicy set fifo");
	if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO))
		return false;
	probe_stage("schedpolicy get");
	if (pthread_attr_getschedpolicy(&attr, &policy))
		return false;
	if (policy != SCHED_FIFO)
		return false;
	probe_stage("schedpolicy invalid inheritsched");
	return pthread_attr_setinheritsched(
		       &attr, 2 * (SCHED_FIFO + SCHED_RR +
				    SCHED_OTHER)) == EINVAL;
}

static void *stackaddr_worker(void *arg)
{
	void *addr = *(void **)arg;
	void *sp;

	probe_stage("stackaddr child entered");
	__asm__ volatile("mov %0, sp" : "=r"(sp));
	if (sp <= addr)
		return (void *)(uintptr_t)1;
	probe_stage("stackaddr child returning");
	return NULL;
}

static bool pthread_stackaddr_create_join_completes(void)
{
	pthread_attr_t attr;
	size_t size;
	void *addr;
	pthread_t thread;
	void *ret = NULL;

	probe_stage("stackaddr attr init");
	if (pthread_attr_init(&attr))
		return false;
	if (pthread_attr_getstacksize(&attr, &size))
		return false;
	addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		return false;
	probe_stage("stackaddr setstack");
	if (pthread_attr_setstack(&attr, addr, size))
		return false;
	if (pthread_attr_setguardsize(&attr, 0))
		return false;
	probe_stage("stackaddr create");
	if (pthread_create(&thread, &attr, stackaddr_worker, &addr))
		return false;
	probe_stage("stackaddr join");
	if (pthread_join(thread, &ret))
		return false;
	probe_stage("stackaddr joined");
	(void)munmap(addr, size);
	return ret == NULL;
}

static bool pthread_stack_round_trip_completes(void)
{
	pthread_attr_t attr;
	void *stackaddr = (void *)1;
	void *new_addr;
	size_t new_size;
	size_t stacksize = PTHREAD_STACK_MIN;

	probe_stage("stack set");
	if (pthread_attr_setstack(&attr, stackaddr, stacksize))
		return false;
	probe_stage("stack get");
	if (pthread_attr_getstack(&attr, &new_addr, &new_size))
		return false;
	return new_addr == stackaddr && new_size == stacksize;
}

int main(void)
{
	orlix_test_plan(9);
	orlix_test_result(pthread_detachstate_round_trip_completes(),
			  "pthread_attr detachstate round trip completes");
	orlix_test_result(pthread_stacksize_create_join_completes(),
			  "pthread_attr stacksize thread create and join completes");
	orlix_test_result(pthread_guardsize_round_trip_completes(),
			  "pthread_attr guardsize round trip completes");
	orlix_test_result(pthread_scope_round_trip_completes(),
			  "pthread_attr scope round trip completes");
	orlix_test_result(pthread_inheritsched_round_trip_completes(),
			  "pthread_attr inheritsched round trip completes");
	orlix_test_result(pthread_schedparam_round_trip_completes(),
			  "pthread_attr schedparam round trip completes");
	orlix_test_result(pthread_schedpolicy_round_trip_completes(),
			  "pthread_attr schedpolicy round trip completes");
	orlix_test_result(pthread_stackaddr_create_join_completes(),
			  "pthread_attr explicit stack thread create and join completes");
	orlix_test_result(pthread_stack_round_trip_completes(),
			  "pthread_attr stack round trip completes");
	orlix_test_exit();
}
