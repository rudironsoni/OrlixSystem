// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/random.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static bool getrandom_returns_requested_bytes(void)
{
	unsigned char buffer[32];
	ssize_t nread = getrandom(buffer, sizeof(buffer), 0);

	return nread == (ssize_t)sizeof(buffer);
}

static bool urandom_returns_requested_bytes(void)
{
	unsigned char buffer[32];
	int fd = open("/dev/urandom", O_RDONLY);
	ssize_t nread;

	if (fd < 0)
		return false;

	nread = read(fd, buffer, sizeof(buffer));
	close(fd);
	return nread == (ssize_t)sizeof(buffer);
}

int main(void)
{
	orlix_test_plan(2);

	orlix_test_result(getrandom_returns_requested_bytes(),
			  "Linux getrandom returns random bytes");
	orlix_test_result(urandom_returns_requested_bytes(),
			  "Linux /dev/urandom returns random bytes");

	orlix_test_exit();
}
