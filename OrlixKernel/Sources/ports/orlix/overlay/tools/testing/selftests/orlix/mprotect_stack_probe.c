// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

#define KIB 1024UL
#define MIB (1024UL * KIB)

static void report_failure(const char *operation, int error)
{
	char line[96];
	size_t pos = 0;

	pos = orlix_append_cstr(line, pos, sizeof(line), "# ");
	pos = orlix_append_cstr(line, pos, sizeof(line), operation);
	pos = orlix_append_cstr(line, pos, sizeof(line), " failed errno=");
	pos = orlix_append_uint(line, pos, sizeof(line), (unsigned int)error);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\n");
	orlix_write_bytes(line, pos);
}

static bool anonymous_prot_none_can_be_mprotected_rw(size_t guard_size,
						     size_t stack_size)
{
	void *mapping;
	volatile char *bytes;
	size_t mapping_size = guard_size + stack_size;
	bool ok = false;

	mapping = mmap(NULL, mapping_size, PROT_NONE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		report_failure("mmap", errno);
		return false;
	}

	if (mprotect((char *)mapping + guard_size, stack_size,
		     PROT_READ | PROT_WRITE) == 0) {
		bytes = mapping;
		bytes[guard_size] = 0x33;
		bytes[guard_size + stack_size - 1] = 0x44;
		ok = bytes[guard_size] == 0x33 &&
		     bytes[guard_size + stack_size - 1] == 0x44;
	} else {
		report_failure("mprotect", errno);
	}

	(void)munmap(mapping, mapping_size);
	return ok;
}

int main(void)
{
	long page_size = sysconf(_SC_PAGESIZE);

	if (page_size <= 0)
		page_size = getpagesize();

	orlix_test_plan(3);
	orlix_test_result(anonymous_prot_none_can_be_mprotected_rw(0, 2 * page_size),
			  "small PROT_NONE anonymous mmap can become writable with mprotect");
	orlix_test_result(anonymous_prot_none_can_be_mprotected_rw(page_size, 2 * MIB),
			  "mlibc default pthread stack mmap can become writable with mprotect");
	orlix_test_result(anonymous_prot_none_can_be_mprotected_rw(page_size, 4 * MIB),
			  "mlibc pthread_attr stack mmap can become writable with mprotect");
	orlix_test_exit();
}
