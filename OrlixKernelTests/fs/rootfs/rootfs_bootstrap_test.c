#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"

#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs/vfs.h"
#include "kernel/init.h"

int close_impl(int fd);
int open_impl(const char *pathname, int flags, mode_t mode);
ssize_t read_impl(int fd, void *buf, size_t count);

static void rootfs_bootstrap_suite_init(struct kunit *test) {
    KUNIT_ASSERT_EQ(test, start_kernel(), 0);
    KUNIT_ASSERT_TRUE(test, kernel_is_booted());
}

static void test_virtual_etc_passwd_exists(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/passwd", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ(test, ret, 0);

    int fd = open_impl("/etc/passwd", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE(test, fd >= 0);
    if (fd >= 0) {
        close_impl(fd);
    }
}

static void test_virtual_etc_group_exists(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/group", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ(test, ret, 0);

    int fd = open_impl("/etc/group", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE(test, fd >= 0);
    if (fd >= 0) {
        close_impl(fd);
    }
}

static void test_virtual_etc_hosts_exists(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/hosts", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ(test, ret, 0);

    int fd = open_impl("/etc/hosts", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE(test, fd >= 0);
    if (fd >= 0) {
        close_impl(fd);
    }
}

static void test_virtual_etc_resolv_conf_exists(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/resolv.conf", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ(test, ret, 0);

    int fd = open_impl("/etc/resolv.conf", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE(test, fd >= 0);
    if (fd >= 0) {
        close_impl(fd);
    }
}

static void test_virtual_etc_passwd_content(struct kunit *test) {
    int fd = open_impl("/etc/passwd", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE(test, fd >= 0);
    if (fd < 0) {
        return;
    }

    char buf[4096];
    ssize_t n = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);

    KUNIT_ASSERT_TRUE(test, n > 0);
    if (n <= 0) {
        return;
    }

    buf[n] = '\0';
    KUNIT_ASSERT_NOT_NULL(test, strstr(buf, "root"));
    KUNIT_ASSERT_NOT_NULL(test, strstr(buf, "orlix:x:1000:1000:"));
}

static void test_virtual_etc_group_content(struct kunit *test) {
    int fd = open_impl("/etc/group", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE(test, fd >= 0);
    if (fd < 0) {
        return;
    }

    char buf[4096];
    ssize_t n = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);

    KUNIT_ASSERT_TRUE(test, n > 0);
    if (n <= 0) {
        return;
    }

    buf[n] = '\0';
    KUNIT_ASSERT_NOT_NULL(test, strstr(buf, "root:x:0:"));
    KUNIT_ASSERT_NOT_NULL(test, strstr(buf, "orlix:x:1000:"));
}

static const struct kunit_case rootfs_bootstrap_cases[] = {
    KUNIT_CASE(test_virtual_etc_passwd_exists),
    KUNIT_CASE(test_virtual_etc_group_exists),
    KUNIT_CASE(test_virtual_etc_hosts_exists),
    KUNIT_CASE(test_virtual_etc_resolv_conf_exists),
    KUNIT_CASE(test_virtual_etc_passwd_content),
    KUNIT_CASE(test_virtual_etc_group_content),
};

static const struct kunit_suite rootfs_bootstrap_suite = {
    .name = "rootfs_bootstrap",
    .cases = rootfs_bootstrap_cases,
    .case_count = sizeof(rootfs_bootstrap_cases) / sizeof(rootfs_bootstrap_cases[0]),
    .init = rootfs_bootstrap_suite_init,
};

const struct kunit_suite *fs_rootfs_bootstrap_suite(void) {
    return &rootfs_bootstrap_suite;
}
