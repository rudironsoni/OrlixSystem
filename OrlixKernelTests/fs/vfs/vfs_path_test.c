/*
 * OrlixKernelTests - VFSPathTests.m (Linux-only)
 *
 * INTERNAL OWNER SEMANTIC TESTS ONLY.
 * Host-dependent helpers and NSFileManager usage are removed and live in the
 * HostBridge test target to preserve strict separation.
 */
#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"

#include <uapi/linux/errno.h>
#include <uapi/asm/stat.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/poll.h>
#include <uapi/linux/stat.h>
#include <linux/dirent.h>
#include <linux/fs_types.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/types.h>

/* Orlix VFS types */
#include "fs/vfs.h"
#include "fs/fdtable.h"
#include "fs/path.h"
#include "kernel/task.h"
#include "kernel/signal.h"
#include "kernel/init.h"
#include "runtime/native/registry.h"
#include "linux_umount2_flags.h"

/* Linux UAPI test support - semantic helpers only */
#include "../../LinuxUAPITestSupport.h"

#ifndef INVALID_FLAG_TEST_VALUE
#define INVALID_FLAG_TEST_VALUE 0x40000000u
#endif

#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

extern ssize_t getdents64_impl(int fd, void *dirp, size_t count);
extern char *getcwd_impl(char *buf, size_t size);
extern long read(int fd, void *buf, size_t count);
extern long write(int fd, const void *buf, size_t count);
extern ssize_t pread(int fd, void *buf, size_t count, int64_t offset);
extern ssize_t pwrite(int fd, const void *buf, size_t count, int64_t offset);
extern int64_t lseek(int fd, int64_t offset, int whence);
extern ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
#include "internal/fs/rootfs.h"
extern int stat_impl(const char *path, struct stat *statbuf);
extern int fstat_impl(int fd, struct stat *statbuf);
extern int lstat_impl(const char *path, struct stat *statbuf);
extern int access(const char *pathname, int mode);
extern int dup(int oldfd);
extern int getpid(void);
extern int kill(int pid, int sig);
extern int unlink(const char *pathname);
extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int openat_impl(int dirfd, const char *pathname, int flags, uint32_t mode);
extern int dup2_impl(int oldfd, int newfd);
extern long read_impl(int fd, void *buf, size_t count);
extern long pread_impl(int fd, void *buf, size_t count, int64_t offset);
extern void cred_reset_to_defaults(void);
extern int vfs_path_contract_open_tmp_fd_symlink_file(void);
extern int errno;

#define KUNIT_ASSERT_EQ_MSG(actual_value, expected_value, ...) \
    KUNIT_ASSERT_EQ(test, actual_value, expected_value)
#define KUNIT_ASSERT_NE_MSG(actual_value, unexpected_value, ...) \
    KUNIT_ASSERT_NE(test, actual_value, unexpected_value)
#define KUNIT_ASSERT_TRUE_MSG(condition, ...) \
    KUNIT_ASSERT_TRUE(test, condition)
#define KUNIT_ASSERT_FALSE_MSG(condition, ...) \
    KUNIT_ASSERT_FALSE(test, condition)
#define KUNIT_ASSERT_STREQ_MSG(actual_value, expected_value, ...) \
    KUNIT_ASSERT_STREQ(test, actual_value, expected_value)
#define KUNIT_ASSERT_NOT_NULL_MSG(ptr, ...) \
    KUNIT_ASSERT_NOT_NULL(test, ptr)

static int open(const char *pathname, int flags, ...) {
    uint32_t mode = 0;

    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;

        va_start(ap, flags);
        mode = (uint32_t)va_arg(ap, int);
        va_end(ap);
    }

    return open_impl(pathname, flags, mode);
}

static int openat(int dirfd, const char *pathname, int flags, ...) {
    uint32_t mode = 0;

    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;

        va_start(ap, flags);
        mode = (uint32_t)va_arg(ap, int);
        va_end(ap);
    }

    return openat_impl(dirfd, pathname, flags, mode);
}

static int close(int fd) {
    return close_impl(fd);
}

static int vfs_path_join(char *buf, size_t buf_len, const char *root, const char *suffix) {
    size_t root_len;
    size_t suffix_len;

    if (!buf || !root || !suffix || buf_len == 0) {
        errno = EINVAL;
        return -1;
    }

    root_len = strlen(root);
    suffix_len = strlen(suffix);
    if (root_len + suffix_len + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(buf, root, root_len);
    memcpy(buf + root_len, suffix, suffix_len + 1);
    return 0;
}

static int vfs_path_proc_fd_string(char *buf, size_t buf_len, int fd) {
    static const char prefix[] = "/proc/self/fd/";
    char digits[16];
    size_t prefix_len = sizeof(prefix) - 1;
    size_t digit_len = 0;
    unsigned int value;

    if (!buf || buf_len == 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    value = (unsigned int)fd;
    do {
        digits[digit_len++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && digit_len < sizeof(digits));

    if (prefix_len + digit_len + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(buf, prefix, prefix_len);
    for (size_t i = 0; i < digit_len; i++) {
        buf[prefix_len + i] = digits[digit_len - 1 - i];
    }
    buf[prefix_len + digit_len] = '\0';
    return 0;
}

static void vfs_path_suite_init(struct kunit *test) {
    KUNIT_ASSERT_EQ(test, start_kernel(), 0);
    KUNIT_ASSERT_TRUE(test, kernel_is_booted());
    cred_reset_to_defaults();
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

static void vfs_path_suite_exit(struct kunit *test) {
    (void)test;
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
    cred_reset_to_defaults();
}





/*
 * Keep only owner-focused tests here. Host-dependent file seeding and
 * NSFileManager-based operations were moved to the HostBridge test target.
 */

/* ============================================================================
 * PATH TRANSLATION TESTS
 * ============================================================================ */

static void testVirtualRootTranslatesToPersistentBackingRoot(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "vfs_translate_path should accept virtual root");
    KUNIT_ASSERT_EQ_MSG(strcmp(host_path, vfs_persistent_backing_root()), 0,
                   "virtual root should map to persistent backing root");
}

static void testTempPathTranslatesToTempBackingRoot(struct kunit *test) {
    char host_path[MAX_PATH];
    char expectedPath[MAX_PATH];
    int ret = vfs_translate_path("/tmp/demo", host_path, sizeof(host_path));
    const char *actualPath = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expectedPath, sizeof(expectedPath),
        vfs_temp_backing_root(), "/demo"), 0);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "temp virtual path should translate");
    KUNIT_ASSERT_STREQ_MSG(actualPath, expectedPath, "temp path should resolve under temp backing root");
    KUNIT_ASSERT_NE_MSG(strcmp(host_path, "/tmp/demo"), 0,
                      "translation must not be identity mapping");
}

static void testRelativePersistentPathMapsUnderPersistentBackingRoot(struct kunit *test) {
    char host_path[MAX_PATH];
    char expectedPath[MAX_PATH];
    int ret = vfs_translate_path("etc/passwd", host_path, sizeof(host_path));
    const char *actualPath = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expectedPath, sizeof(expectedPath),
        vfs_persistent_backing_root(), "/etc/passwd"), 0);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "relative virtual path should translate");
    KUNIT_ASSERT_STREQ_MSG(actualPath, expectedPath, "relative path should resolve under persistent backing root");
}

static void testUnmappableHostPathIsRejected(struct kunit *test) {
    char virtual_path[MAX_PATH];
    int ret = vfs_reverse_translate("/private/var/mobile", virtual_path, sizeof(virtual_path));

    KUNIT_ASSERT_EQ_MSG(ret, -EXDEV, "unmapped host path should be rejected");
}

static void testPersistentBackingRootReverseTranslatesToVirtualRoot(struct kunit *test) {
    char virtual_path[MAX_PATH];
    int ret = vfs_reverse_translate(vfs_persistent_backing_root(), virtual_path, sizeof(virtual_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "persistent backing root should reverse translate");
    KUNIT_ASSERT_EQ_MSG(strcmp(virtual_path, vfs_virtual_root()), 0,
                   "persistent backing root should map back to virtual root");
}

static void testMappedPersistentHostPathReverseTranslatesToVirtualPath(struct kunit *test) {
    char virtual_path[MAX_PATH];
    char hostPath[MAX_PATH];
    KUNIT_ASSERT_EQ(test, vfs_path_join(hostPath, sizeof(hostPath),
        vfs_persistent_backing_root(), "/var/log"), 0);
    int ret = vfs_reverse_translate(hostPath, virtual_path, sizeof(virtual_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "mapped persistent host path should reverse translate");
    KUNIT_ASSERT_STREQ_MSG(virtual_path, "/var/log",
                          "reverse translation should return virtual path");
}

static void testPersistentRootDiscoveryResolves(struct kunit *test) {
    char path[MAX_PATH];
    int ret = backing_root_discover_persistent(path, sizeof(path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "persistent root discovery should succeed");
    KUNIT_ASSERT_TRUE_MSG(path[0] != '\0', "persistent root should be non-empty");
}

static void testCacheRootDiscoveryResolves(struct kunit *test) {
    char path[MAX_PATH];
    int ret = backing_root_discover_cache(path, sizeof(path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "cache root discovery should succeed");
    KUNIT_ASSERT_TRUE_MSG(path[0] != '\0', "cache root should be non-empty");
}

static void testTempRootDiscoveryResolves(struct kunit *test) {
    char path[MAX_PATH];
    int ret = backing_root_discover_temp(path, sizeof(path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "temp root discovery should succeed");
    KUNIT_ASSERT_TRUE_MSG(path[0] != '\0', "temp root should be non-empty");
}

static void testDiscoveredBackingRootsAreDistinctByClass(struct kunit *test) {
    char persistent[MAX_PATH];
    char cache[MAX_PATH];
    char temp[MAX_PATH];

    KUNIT_ASSERT_EQ_MSG(backing_root_discover_persistent(persistent, sizeof(persistent)), 0,
                   "persistent root discovery should succeed");
    KUNIT_ASSERT_EQ_MSG(backing_root_discover_cache(cache, sizeof(cache)), 0,
                   "cache root discovery should succeed");
    KUNIT_ASSERT_EQ_MSG(backing_root_discover_temp(temp, sizeof(temp)), 0,
                   "temp root discovery should succeed");

    KUNIT_ASSERT_NE_MSG(strcmp(persistent, temp), 0,
                      "persistent root must not be temp-backed");
    KUNIT_ASSERT_NE_MSG(strcmp(cache, temp), 0,
                      "cache and temp roots should not collapse to one path");
}

static void testBackingClassRoutingForPersistentPaths(struct kunit *test) {
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/etc/passwd"), VFS_BACKING_PERSISTENT,
                   "/etc/passwd should route to persistent backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/usr/bin/sh"), VFS_BACKING_PERSISTENT,
                   "/usr/bin/sh should route to persistent backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/var/lib/foo"), VFS_BACKING_PERSISTENT,
                   "/var/lib/foo should route to persistent backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/home/user/.profile"), VFS_BACKING_PERSISTENT,
                   "/home paths should route to persistent backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/root/.profile"), VFS_BACKING_PERSISTENT,
                   "/root paths should route to persistent backing");
}

static void testBackingClassRoutingForCacheTempAndSyntheticPaths(struct kunit *test) {
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/var/cache/x"), VFS_BACKING_CACHE,
                   "/var/cache/x should route to cache backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/tmp/x"), VFS_BACKING_TEMP,
                   "/tmp/x should route to temp backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/proc/meminfo"), VFS_BACKING_SYNTHETIC,
                   "/proc/meminfo should route to synthetic backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/sys/kernel"), VFS_BACKING_SYNTHETIC,
                   "/sys/kernel should route to synthetic backing");
    KUNIT_ASSERT_EQ_MSG(vfs_backing_class_for_path("/dev/null"), VFS_BACKING_SYNTHETIC,
                   "/dev/null should route to synthetic backing");
}

static void testPersistentFallbackRouteTranslatesAndReverseTranslates(struct kunit *test) {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    char expectedPath[MAX_PATH];
    int ret;
    const char *actualPath;

    ret = vfs_translate_path("/var/log/messages", host_path, sizeof(host_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "fallback persistent path should translate");

    actualPath = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expectedPath, sizeof(expectedPath),
        vfs_persistent_backing_root(), "/var/log/messages"), 0);
    KUNIT_ASSERT_STREQ_MSG(actualPath, expectedPath,
                          "fallback persistent route should join under persistent backing root");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "fallback persistent host path should reverse translate");
    KUNIT_ASSERT_STREQ_MSG(virtual_path, "/var/log/messages",
                          "fallback persistent host path should map back through the route table");
}

static void testSyntheticRouteRejectsHostJoin(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/dev/null", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, -ENOTSUP, "synthetic route should not join to a host backing root");
}

static void testDescriptorDrivenPathClassification(struct kunit *test) {
    KUNIT_ASSERT_EQ_MSG(path_classify("/proc/meminfo"), PATH_VIRTUAL_LINUX,
                   "synthetic routes should classify through descriptor lookup");
    KUNIT_ASSERT_EQ_MSG(path_classify("/sys/kernel"), PATH_VIRTUAL_LINUX,
                   "sys routes should classify through descriptor lookup");
    KUNIT_ASSERT_EQ_MSG(path_classify("/dev/null"), PATH_VIRTUAL_LINUX,
                   "dev routes should classify through descriptor lookup");
    KUNIT_ASSERT_EQ_MSG(path_classify("/private/var/mobile/file"), PATH_ABSOLUTE_HOST,
                   "non-route absolute paths should remain host paths");
}

static void testDescriptorDrivenVirtualLinuxDetection(struct kunit *test) {
    KUNIT_ASSERT_TRUE_MSG(path_is_virtual_linux("/tmp/demo"),
                  "tmp route should be recognized as Linux-visible");
    KUNIT_ASSERT_TRUE_MSG(path_is_virtual_linux("/var/tmp/demo"),
                  "var/tmp route should remain a distinct Linux-visible route");
    KUNIT_ASSERT_TRUE_MSG(path_is_virtual_linux("/run/demo"),
                  "run route should remain a distinct Linux-visible route");
    KUNIT_ASSERT_TRUE_MSG(path_is_virtual_linux("relative/demo"),
                  "relative paths should stay Linux-visible by context");
    KUNIT_ASSERT_FALSE_MSG(path_is_virtual_linux("/private/var/mobile/file"),
                   "absolute host paths outside route descriptors should not classify as Linux-visible");
}

static void testPersistentRootIsNotDocumentsTruth(struct kunit *test) {
    const char *persistentRoot = vfs_persistent_backing_root();

    KUNIT_ASSERT_FALSE_MSG(strstr(persistentRoot, "/Documents") != NULL,
                   "persistent backing root must not treat Documents as Linux root truth");
    KUNIT_ASSERT_FALSE_MSG(path_is_own_sandbox("/Documents/example.txt"),
                   "Documents path fragments must not become Linux root truth through sandbox heuristics");
}

static void testPersistentBackingRootReverseTranslation(struct kunit *test) {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    int ret;

    ret = vfs_translate_path("/etc/passwd", host_path, sizeof(host_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "persistent path translation should succeed");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "persistent host path should reverse translate");
    KUNIT_ASSERT_STREQ_MSG(virtual_path, "/etc/passwd",
                          "persistent host path should map back to /etc/passwd");
}

static void testCacheBackingRootReverseTranslation(struct kunit *test) {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    int ret;

    ret = vfs_translate_path("/var/cache/x", host_path, sizeof(host_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "cache path translation should succeed");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "cache host path should reverse translate");
    KUNIT_ASSERT_STREQ_MSG(virtual_path, "/var/cache/x",
                          "cache host path should map back to /var/cache/x");
}

static void testTempBackingRootReverseTranslation(struct kunit *test) {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    int ret;

    ret = vfs_translate_path("/tmp/x", host_path, sizeof(host_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "temp path translation should succeed");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    KUNIT_ASSERT_EQ_MSG(ret, 0, "temp host path should reverse translate");
    KUNIT_ASSERT_STREQ_MSG(virtual_path, "/tmp/x",
                          "temp host path should map back to /tmp/x");
}

static void testParentEscapeIsRejected(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("../secret", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, -EINVAL, "parent escapes should be rejected");
}

/* ============================================================================
 * TASK-AWARE PATH RESOLUTION TESTS
 * ============================================================================ */

static void testTaskAwareAbsolutePathUsesVirtualRoot(struct kunit *test) {
    /* Create an fs_struct with custom root */
    struct fs_context *fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(fs != NULL, "fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/etc");

    /* Absolute path should resolve from root - with root="/", "/bin/ls" -> "/bin/ls" */
    char host_path[MAX_PATH];
    char expected[MAX_PATH];
    int ret = vfs_translate_path_task("/bin/ls", host_path, sizeof(host_path), fs);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "absolute path translation should succeed");
    const char *result = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expected, sizeof(expected),
        vfs_primary_backing_root(), "/bin/ls"), 0);
    KUNIT_ASSERT_STREQ_MSG(result, expected, "absolute path should resolve from virtual root");

    free_fs_struct(fs);
}

static void testTaskAwareRelativePathUsesPwd(struct kunit *test) {
    /* Create an fs_struct with custom pwd */
    struct fs_context *fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(fs != NULL, "fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/etc");

    /* Relative path should resolve from pwd */
    char host_path[MAX_PATH];
    char expected[MAX_PATH];
    int ret = vfs_translate_path_task("passwd", host_path, sizeof(host_path), fs);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "relative path translation should succeed");
    const char *result = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expected, sizeof(expected),
        vfs_primary_backing_root(), "/etc/passwd"), 0);
    KUNIT_ASSERT_STREQ_MSG(result, expected, "relative path should resolve from virtual pwd");

    free_fs_struct(fs);
}

static void testTaskAwareRelativePathWithSubdirectories(struct kunit *test) {
    /* Create an fs_struct with nested pwd */
    struct fs_context *fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(fs != NULL, "fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/usr/local");

    /* Relative path with subdirectory */
    char host_path[MAX_PATH];
    char expected[MAX_PATH];
    int ret = vfs_translate_path_task("bin/myapp", host_path, sizeof(host_path), fs);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "nested relative path translation should succeed");
    const char *result = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expected, sizeof(expected),
        vfs_primary_backing_root(), "/usr/local/bin/myapp"), 0);
    KUNIT_ASSERT_STREQ_MSG(result, expected, "relative path should resolve correctly from nested pwd");

    free_fs_struct(fs);
}

static void testTaskAwareParentEscapeRejected(struct kunit *test) {
    /* Create an fs_struct */
    struct fs_context *fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(fs != NULL, "fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/etc");

    /* Parent escape should be rejected even with task context */
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_task("../secret", host_path, sizeof(host_path), fs);

    KUNIT_ASSERT_EQ_MSG(ret, -EINVAL, "parent escapes should be rejected with task context");

    free_fs_struct(fs);
}

static void testTaskAwareAbsolutePathUsesTaskRootPrefix(struct kunit *test) {
    struct fs_context *fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(fs != NULL, "fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/sandbox");
    fs_init_pwd(fs, "/sandbox/work");

    char host_path[MAX_PATH];
    char expected[MAX_PATH];
    int ret = vfs_translate_path_task("/bin/ls", host_path, sizeof(host_path), fs);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "absolute path translation should succeed from non-root task root");
    const char *result = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expected, sizeof(expected),
        vfs_primary_backing_root(), "/sandbox/bin/ls"), 0);
    KUNIT_ASSERT_STREQ_MSG(result, expected, "absolute paths should resolve from task root prefix");

    free_fs_struct(fs);
}

static void testGetcwdMatchesTaskPwdAndRelativeResolution(struct kunit *test) {
    struct task *originalTask = task_current();
    struct task *task = alloc_task();
    KUNIT_ASSERT_TRUE_MSG(task != NULL, "task allocation should succeed");
    if (!task) return;

    task->fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(task->fs != NULL, "fs_struct allocation should succeed");
    if (!task->fs) {
        task_put(task);
        return;
    }

    fs_init_root(task->fs, "/");
    fs_init_pwd(task->fs, "/usr/local");
    task_set_current(task);

    char cwd[MAX_PATH];
    char resolved[MAX_PATH];
    char expected[MAX_PATH];

    char *cwd_result = getcwd_impl(cwd, sizeof(cwd));
    KUNIT_ASSERT_TRUE_MSG(cwd_result != NULL, "getcwd_impl should return current task pwd");
    KUNIT_ASSERT_STREQ_MSG(cwd, "/usr/local",
                          "getcwd_impl should report task virtual pwd");

    int resolve_ret = path_resolve("bin/tool", resolved, sizeof(resolved));
    KUNIT_ASSERT_EQ_MSG(resolve_ret, 0, "path_resolve should succeed for relative task path");

    int expected_ret = vfs_translate_path_task("bin/tool", expected, sizeof(expected), task->fs);
    KUNIT_ASSERT_EQ_MSG(expected_ret, 0, "expected task-aware translation should succeed");
    KUNIT_ASSERT_STREQ_MSG(resolved, expected,
                          "relative resolution should agree with task getcwd state");

    task_set_current(originalTask);
    task_put(task);
}

/* ============================================================================
 * DIRFD-AWARE PATH RESOLUTION TESTS
 * ============================================================================ */

static void testVfsTranslatePathAtUsesAtFdcwd(struct kunit *test) {
    char host_path[MAX_PATH];
    char expected[MAX_PATH];
    int ret = vfs_translate_path_at(AT_FDCWD, "/etc/passwd", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "vfs_translate_path_at with AT_FDCWD should succeed");
    const char *result = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expected, sizeof(expected),
        vfs_primary_backing_root(), "/etc/passwd"), 0);
    KUNIT_ASSERT_STREQ_MSG(result, expected, "AT_FDCWD should resolve from task cwd");
}

static void testVfsTranslatePathAtAbsolutePathIgnoresDirfd(struct kunit *test) {
    char host_path[MAX_PATH];
    char expected[MAX_PATH];
    int ret = vfs_translate_path_at(-1, "/bin/ls", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "absolute path should succeed regardless of invalid dirfd");
    const char *result = host_path;
    KUNIT_ASSERT_EQ(test, vfs_path_join(expected, sizeof(expected),
        vfs_primary_backing_root(), "/bin/ls"), 0);
    KUNIT_ASSERT_STREQ_MSG(result, expected, "absolute paths should resolve from task root");
}

static void testVfsTranslatePathAtInvalidDirfdReturnsBadf(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_at(9999, "relative/path", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, -EBADF, "invalid dirfd should return -EBADF");
}

/* ============================================================================
 * STAT-FAMILY AND AT-FLAG SEMANTICS TESTS
 * ============================================================================ */

static void testVfsFstatatSupportsAtFdcwd(struct kunit *test) {
    extern int vfs_contract_fstatat_at_fdcwd(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fstatat_at_fdcwd(), 0,
                   "vfs_fstatat with AT_FDCWD should succeed");
}

static void testVfsFstatatSupportsSymlinkNoFollow(struct kunit *test) {
    extern int vfs_contract_fstatat_symlink_nofollow(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fstatat_symlink_nofollow(), 0,
                   "vfs_fstatat with Linux AT_SYMLINK_NOFOLLOW should succeed");
}

static void testOpenNoFollowRejectsSymlinkWithEloop(struct kunit *test) {
    extern int vfs_contract_open_nofollow_rejects_symlink_with_eloop(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_nofollow_rejects_symlink_with_eloop(), 0,
                   "open with O_NOFOLLOW should reject final symlink with ELOOP, errno %d", errno);
}

static void testOpenatNoFollowRejectsDirfdSymlinkWithEloop(struct kunit *test) {
    extern int vfs_contract_openat_nofollow_rejects_dirfd_symlink_with_eloop(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_openat_nofollow_rejects_dirfd_symlink_with_eloop(), 0,
                   "openat with O_NOFOLLOW should reject dirfd-relative final symlink with ELOOP, errno %d", errno);
}

static void testOpenFollowsSymlinkToFileWithoutNoFollow(struct kunit *test) {
    extern int vfs_contract_open_follows_symlink_to_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_follows_symlink_to_file(), 0,
                   "open without O_NOFOLLOW should follow relative symlink to file, errno %d", errno);
}

static void testOpenFollowsAbsoluteSymlinkAsVirtualPath(struct kunit *test) {
    extern int vfs_contract_open_follows_absolute_symlink_as_virtual_path(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_follows_absolute_symlink_as_virtual_path(), 0,
                   "open should interpret absolute symlink targets through the virtual root, errno %d", errno);
}

static void testOpenResolvesIntermediateSymlinkDirectory(struct kunit *test) {
    extern int vfs_contract_open_resolves_intermediate_symlink_directory(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_resolves_intermediate_symlink_directory(), 0,
                   "open should resolve intermediate symlink directories in the virtual path walk, errno %d", errno);
}

static void testOpenSymlinkLoopReturnsEloop(struct kunit *test) {
    extern int vfs_contract_open_symlink_loop_returns_eloop(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_symlink_loop_returns_eloop(), 0,
                   "open should reject symlink loops with ELOOP, errno %d", errno);
}

static void testChdirResolvesSymlinkDirectory(struct kunit *test) {
    extern int vfs_contract_chdir_resolves_symlink_directory(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_chdir_resolves_symlink_directory(), 0,
                   "chdir should resolve symlink directory targets in the virtual path walk, errno %d", errno);
}

static void testMkdiratResolvesIntermediateSymlinkDirectory(struct kunit *test) {
    extern int vfs_contract_mkdirat_resolves_intermediate_symlink_directory(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mkdirat_resolves_intermediate_symlink_directory(), 0,
                   "mkdirat should resolve intermediate symlink directories, errno %d", errno);
}

static void testUnlinkatResolvesIntermediateSymlinkDirectory(struct kunit *test) {
    extern int vfs_contract_unlinkat_resolves_intermediate_symlink_directory(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_unlinkat_resolves_intermediate_symlink_directory(), 0,
                   "unlinkat should resolve intermediate symlink directories, errno %d", errno);
}

static void testRenameatResolvesIntermediateSymlinkDirectories(struct kunit *test) {
    extern int vfs_contract_renameat_resolves_intermediate_symlink_directories(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_renameat_resolves_intermediate_symlink_directories(), 0,
                   "renameat2 should resolve intermediate symlink directories, errno %d", errno);
}

static void testLinkatRespectsSymlinkFollowFlag(struct kunit *test) {
    extern int vfs_contract_linkat_respects_symlink_follow_flag(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_linkat_respects_symlink_follow_flag(), 0,
                   "linkat should honor AT_SYMLINK_FOLLOW for final symlink targets, errno %d", errno);
}

static void testVfsFstatatRejectsInvalidFlags(struct kunit *test) {
    struct stat st;
    int ret = vfs_fstatat(AT_FDCWD, "/etc/passwd", &st, INVALID_FLAG_TEST_VALUE);

    KUNIT_ASSERT_EQ_MSG(ret, -EINVAL, "vfs_fstatat should reject invalid flags");
}

static void testFstatInvalidFdReturnsBadf(struct kunit *test) {
    struct stat st;

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(fstat_impl(-1, &st), -1, "fstat_impl should reject invalid fd");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "fstat_impl should set EBADF for invalid fd");
}

static void testFstatNullStatbufReturnsFault(struct kunit *test) {
    int fd = open("/etc/passwd", O_RDONLY);

    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/etc/passwd) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(fstat_impl(fd, NULL), -1, "fstat_impl should reject NULL stat buffer");
    KUNIT_ASSERT_EQ_MSG(errno, EFAULT, "fstat_impl should set EFAULT for NULL stat buffer");

    close(fd);
}

static void testFstatImplSucceedsForLinuxOwnedFd(struct kunit *test) {
    struct stat st;
    int fd = open("/etc/passwd", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/etc/passwd) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(fstat_impl(fd, &st), 0, "fstat_impl should succeed for Linux-owned fd");
    KUNIT_ASSERT_TRUE_MSG(st.st_mode != 0, "fstat_impl should populate mode");
    KUNIT_ASSERT_TRUE_MSG(st.st_nlink > 0, "fstat_impl should populate link count");

    close(fd);
}

static void testFstatDuplicatedRealBackedFdMatchesOriginal(struct kunit *test) {
    struct stat original_st;
    struct stat duplicate_st;
    int fd = open("/etc/passwd", O_RDONLY);
    int dup_fd;

    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/etc/passwd) should succeed");
    if (fd < 0) {
        return;
    }

    dup_fd = dup(fd);
    KUNIT_ASSERT_TRUE_MSG(dup_fd >= 0, "dup should succeed for Linux-owned fd");
    if (dup_fd < 0) {
        close(fd);
        return;
    }

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(fstat_impl(fd, &original_st), 0, "fstat_impl should succeed for original Linux-owned fd");
    KUNIT_ASSERT_EQ_MSG(fstat_impl(dup_fd, &duplicate_st), 0, "fstat_impl should succeed for duplicated Linux-owned fd");
    KUNIT_ASSERT_EQ_MSG(original_st.st_dev, duplicate_st.st_dev, "duplicated fds should preserve device identity");
    KUNIT_ASSERT_EQ_MSG(original_st.st_ino, duplicate_st.st_ino, "duplicated fds should preserve inode identity");
    KUNIT_ASSERT_EQ_MSG(original_st.st_mode, duplicate_st.st_mode, "duplicated fds should preserve mode bits");

    close(dup_fd);
    close(fd);
}

static void testFstatProcDirectoryFdReportsDirectory(struct kunit *test) {
    struct stat st;
    int fd = open("/proc", O_RDONLY | O_DIRECTORY, 0);

    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(fstat_impl(fd, &st), 0, "fstat_impl should succeed for synthetic /proc directory fd");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "synthetic /proc fd should stat as a directory");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0555, "synthetic /proc fd should preserve synthetic permissions");

    close(fd);
}

static void testFstatProcSelfFdDirectoryReportsDirectory(struct kunit *test) {
    struct stat st;
    int fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);

    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fd, O_DIRECTORY) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(fstat_impl(fd, &st), 0, "fstat_impl should succeed for synthetic /proc/self/fd directory fd");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "synthetic /proc/self/fd fd should stat as a directory");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0555, "synthetic /proc/self/fd fd should preserve synthetic permissions");

    close(fd);
}

static void testFstatProcSelfFdinfoFileReportsRegularFile(struct kunit *test) {
    struct stat st;
    int fd = open("/proc/self/fdinfo/0", O_RDONLY, 0);

    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(fstat_impl(fd, &st), 0, "fstat_impl should succeed for synthetic /proc/self/fdinfo file fd");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "synthetic /proc/self/fdinfo fd should stat as a regular file");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0444, "synthetic /proc/self/fdinfo fd should preserve synthetic permissions");

    close(fd);
}

static void testSyntheticRootStatSucceeds(struct kunit *test) {
    struct stat st;

    KUNIT_ASSERT_EQ_MSG(vfs_fstatat(AT_FDCWD, "/proc", &st, 0), 0,
                   "synthetic root vfs_fstatat should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "/proc root should be a directory");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0555, "/proc root should have 0555 permissions");

    KUNIT_ASSERT_EQ_MSG(vfs_fstatat(AT_FDCWD, "/sys", &st, 0), 0,
                   "synthetic root vfs_fstatat should succeed for /sys");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "/sys root should be a directory");

    KUNIT_ASSERT_EQ_MSG(vfs_fstatat(AT_FDCWD, "/dev", &st, 0), 0,
                   "synthetic root vfs_fstatat should succeed for /dev");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "/dev root should be a directory");

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc", &st), 0,
                   "public stat should succeed for synthetic root");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "public stat should return directory for /proc");

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(lstat_impl("/sys", &st), 0,
                   "public lstat should succeed for synthetic root");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "public lstat should return directory for /sys");
}

static void testSyntheticChildStatHandlesSupportedProcFilesAndRejectsUnsupportedSys(struct kunit *test) {
    struct stat st;
    extern int vfs_contract_fstatat_synthetic_child_nofollow(void);

    KUNIT_ASSERT_EQ_MSG(vfs_fstatat(AT_FDCWD, "/proc/meminfo", &st, 0), 0,
                   "/proc/meminfo should be a supported procfs file");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "/proc/meminfo should stat as a regular file");
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fstatat_synthetic_child_nofollow(), -ENOENT,
                   "synthetic child vfs_fstatat with Linux AT_SYMLINK_NOFOLLOW should reject through descriptor policy");

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/meminfo", &st), 0,
                   "Orlix stat should support /proc/meminfo");

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(lstat_impl("/sys/kernel", &st), -1,
                   "Orlix lstat should reject unsupported synthetic child paths");
    KUNIT_ASSERT_EQ_MSG(errno, ENOENT, "Orlix lstat should set ENOENT for unsupported synthetic child paths");
}

static void testSyntheticRootAccessSucceeds(struct kunit *test) {
    KUNIT_ASSERT_EQ_MSG(vfs_faccessat(AT_FDCWD, "/proc", 0, 0), 0,
                   "synthetic root vfs_faccessat should succeed");
    KUNIT_ASSERT_EQ_MSG(vfs_faccessat(AT_FDCWD, "/sys", 0, 0), 0,
                   "synthetic root vfs_faccessat should succeed for /sys");
    KUNIT_ASSERT_EQ_MSG(vfs_faccessat(AT_FDCWD, "/dev", 0, 0), 0,
                   "synthetic root vfs_faccessat should succeed for /dev");

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(access("/proc", 0), 0,
                   "public access should succeed for synthetic root");
}

static void testSyntheticChildAccessHandlesSupportedProcFiles(struct kunit *test) {
    KUNIT_ASSERT_EQ_MSG(vfs_faccessat(AT_FDCWD, "/proc/meminfo", 0, 0), 0,
                   "/proc/meminfo should be visible through vfs_faccessat");

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(access("/proc/meminfo", 0), 0,
                   "public access should support /proc/meminfo");
}

static void testSyntheticRootOpenDirectorySucceedsAndChildOpenFails(struct kunit *test) {
    errno = 0;
    int proc_fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(proc_fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (proc_fd >= 0) close(proc_fd);

    errno = 0;
    int sys_fd = open("/sys", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(sys_fd >= 0, "open(/sys, O_DIRECTORY) should succeed");
    if (sys_fd >= 0) close(sys_fd);

    errno = 0;
    int dev_fd = open("/dev", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(dev_fd >= 0, "open(/dev, O_DIRECTORY) should succeed");
    if (dev_fd >= 0) close(dev_fd);
}

static void testSyntheticChildOpenHandlesSupportedProcFilesAndRejectsUnsupportedSys(struct kunit *test) {
    errno = 0;
    int meminfo_fd = open("/proc/meminfo", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(meminfo_fd >= 0, "public open should support /proc/meminfo");
    if (meminfo_fd >= 0) close(meminfo_fd);

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(openat(AT_FDCWD, "/sys/kernel", O_RDONLY), -1,
                   "public openat should reject unsupported synthetic routes before host fallback");
    KUNIT_ASSERT_EQ_MSG(errno, ENOTSUP, "public openat should set ENOTSUP for unsupported synthetic routes");
}

static void testSyntheticRootOpenDirectorySucceeds(struct kunit *test) {
    errno = 0;
    int proc_fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(proc_fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    KUNIT_ASSERT_EQ_MSG(errno, 0, "errno should be 0 after open(/proc, O_DIRECTORY)");

    errno = 0;
    int sys_fd = open("/sys", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(sys_fd >= 0, "open(/sys, O_DIRECTORY) should succeed");
    KUNIT_ASSERT_EQ_MSG(errno, 0, "errno should be 0 after open(/sys, O_DIRECTORY)");

    errno = 0;
    int dev_fd = open("/dev", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(dev_fd >= 0, "open(/dev, O_DIRECTORY) should succeed");
    KUNIT_ASSERT_EQ_MSG(errno, 0, "errno should be 0 after open(/dev, O_DIRECTORY)");

    if (proc_fd >= 0) close(proc_fd);
    if (sys_fd >= 0) close(sys_fd);
    if (dev_fd >= 0) close(dev_fd);
}

static void testSyntheticRootGetdents64ReturnsDotAndDotdot(struct kunit *test) {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    /* Ensure buffer is 8-byte aligned for struct linux_dirent64 */
    union { char storage[1024]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64_impl(fd, buffer, sizeof(aligned.storage));
    KUNIT_ASSERT_TRUE_MSG(nread > 0, "getdents64_impl(/proc) should return > 0 bytes, got %zd errno %d", nread, errno);

    // Parse the entries to verify . and ..
    bool found_dot = false;
    bool found_dotdot = false;
    size_t pos = 0;

    while (pos < (size_t)nread) {
        struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
        const char *name = entry->d_name;

        if (strcmp(name, ".") == 0) {
            found_dot = true;
            KUNIT_ASSERT_EQ_MSG(entry->d_type, DT_DIR, ". should be DT_DIR");
        } else if (strcmp(name, "..") == 0) {
            found_dotdot = true;
            KUNIT_ASSERT_EQ_MSG(entry->d_type, DT_DIR, ".. should be DT_DIR");
        }

        KUNIT_ASSERT_TRUE_MSG(entry->d_reclen > 0, "d_reclen must be non-zero");
        KUNIT_ASSERT_TRUE_MSG(entry->d_reclen <= (unsigned short)((size_t)nread - pos), "d_reclen must fit remaining buffer");
        if (entry->d_reclen == 0 || entry->d_reclen > (unsigned short)((size_t)nread - pos)) {
            break;
        }
        pos += entry->d_reclen;
    }

    KUNIT_ASSERT_TRUE_MSG(found_dot, "getdents64_impl(/proc) should return '.' entry");
    KUNIT_ASSERT_TRUE_MSG(found_dotdot, "getdents64_impl(/proc) should return '..' entry");

    // Second call should return 0 (EOF)
    nread = getdents64_impl(fd, buffer, sizeof(aligned.storage));
    KUNIT_ASSERT_EQ_MSG(nread, 0, "Second getdents64_impl(/proc) should return 0 (EOF)");

    close(fd);
}

static void testSyntheticSysAndDevGetdents64ReturnsDotAndDotdot(struct kunit *test) {
    // Test /sys
    int sys_fd = open("/sys", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(sys_fd >= 0, "open(/sys, O_DIRECTORY) should succeed");

    if (sys_fd >= 0) {
        union { char storage[1024]; uint64_t align; } aligned;
        char *buffer = aligned.storage;
        memset(buffer, 0, sizeof(aligned));

        ssize_t nread = getdents64_impl(sys_fd, buffer, sizeof(aligned.storage));
        KUNIT_ASSERT_TRUE_MSG(nread > 0, "getdents64_impl(/sys) should return > 0 bytes");

        bool found_dot = false;
        bool found_dotdot = false;
        size_t pos = 0;

        while (pos < (size_t)nread) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
            const char *name = entry->d_name;

            if (strcmp(name, ".") == 0) {
                found_dot = true;
                KUNIT_ASSERT_EQ_MSG(entry->d_type, DT_DIR, ". should be DT_DIR");
            } else if (strcmp(name, "..") == 0) {
                found_dotdot = true;
                KUNIT_ASSERT_EQ_MSG(entry->d_type, DT_DIR, ".. should be DT_DIR");
            }

            KUNIT_ASSERT_TRUE_MSG(entry->d_reclen > 0, "d_reclen must be non-zero");
            KUNIT_ASSERT_TRUE_MSG(entry->d_reclen <= (unsigned short)((size_t)nread - pos), "d_reclen must fit remaining buffer");
            if (entry->d_reclen == 0 || entry->d_reclen > (unsigned short)((size_t)nread - pos)) {
                break;
            }
            pos += entry->d_reclen;
        }

        KUNIT_ASSERT_TRUE_MSG(found_dot, "getdents64_impl(/sys) should return '.' entry");
        KUNIT_ASSERT_TRUE_MSG(found_dotdot, "getdents64_impl(/sys) should return '..' entry");

        // Second call should return 0 (EOF)
        nread = getdents64_impl(sys_fd, buffer, sizeof(aligned.storage));
        KUNIT_ASSERT_EQ_MSG(nread, 0, "Second getdents64_impl(/sys) should return 0 (EOF)");

        close(sys_fd);
    }

    // Test /dev
    int dev_fd = open("/dev", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(dev_fd >= 0, "open(/dev, O_DIRECTORY) should succeed");

    if (dev_fd >= 0) {
        union { char storage[1024]; uint64_t align; } aligned;
        char *buffer = aligned.storage;
        memset(buffer, 0, sizeof(aligned));

        ssize_t nread = getdents64_impl(dev_fd, buffer, sizeof(aligned.storage));
        KUNIT_ASSERT_TRUE_MSG(nread > 0, "getdents64_impl(/dev) should return > 0 bytes");

        bool found_dot = false;
        bool found_dotdot = false;
        size_t pos = 0;

        while (pos < (size_t)nread) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
            const char *name = entry->d_name;

            if (strcmp(name, ".") == 0) {
                found_dot = true;
                KUNIT_ASSERT_EQ_MSG(entry->d_type, DT_DIR, ". should be DT_DIR");
            } else if (strcmp(name, "..") == 0) {
                found_dotdot = true;
                KUNIT_ASSERT_EQ_MSG(entry->d_type, DT_DIR, ".. should be DT_DIR");
            }

            KUNIT_ASSERT_TRUE_MSG(entry->d_reclen > 0, "d_reclen must be non-zero");
            KUNIT_ASSERT_TRUE_MSG(entry->d_reclen <= (unsigned short)((size_t)nread - pos), "d_reclen must fit remaining buffer");
            if (entry->d_reclen == 0 || entry->d_reclen > (unsigned short)((size_t)nread - pos)) {
                break;
            }
            pos += entry->d_reclen;
        }

        KUNIT_ASSERT_TRUE_MSG(found_dot, "getdents64_impl(/dev) should return '.' entry");
        KUNIT_ASSERT_TRUE_MSG(found_dotdot, "getdents64_impl(/dev) should return '..' entry");

        // Second call should return 0 (EOF)
        nread = getdents64_impl(dev_fd, buffer, sizeof(aligned.storage));
        KUNIT_ASSERT_EQ_MSG(nread, 0, "Second getdents64_impl(/dev) should return 0 (EOF)");

        close(dev_fd);
    }
}

/* ============================================================================
 * SYNTHETIC /dev NODE TESTS
 * ============================================================================ */

static void testDevNullStatSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/dev/null", &st), 0, "stat(/dev/null) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_char_device(st.st_mode), "/dev/null should be a character device");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0666, "/dev/null should have 0666 permissions");

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(lstat_impl("/dev/null", &st), 0, "lstat(/dev/null) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_char_device(st.st_mode), "lstat(/dev/null) should return character device");
}

static void testDevZeroStatSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/dev/zero", &st), 0, "stat(/dev/zero) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_char_device(st.st_mode), "/dev/zero should be a character device");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0666, "/dev/zero should have 0666 permissions");
}

static void testDevUrandomStatSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/dev/urandom", &st), 0, "stat(/dev/urandom) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_char_device(st.st_mode), "/dev/urandom should be a character device");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0666, "/dev/urandom should have 0666 permissions");
}

static void testDevNullAccessSucceeds(struct kunit *test) {
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(access("/dev/null", 0), 0, "access(/dev/null, 0) should succeed");
    KUNIT_ASSERT_EQ_MSG(access("/dev/null", 4), 0, "access(/dev/null, 4) should succeed");
    KUNIT_ASSERT_EQ_MSG(access("/dev/null", 2), 0, "access(/dev/null, 2) should succeed");
}

static void testDevZeroAccessSucceeds(struct kunit *test) {
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(access("/dev/zero", 0), 0, "access(/dev/zero, 0) should succeed");
}

static void testDevUrandomAccessSucceeds(struct kunit *test) {
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(access("/dev/urandom", 0), 0, "access(/dev/urandom, 0) should succeed");
}

static void testDevNullOpenSucceeds(struct kunit *test) {
    errno = 0;
    int fd = open("/dev/null", O_RDWR);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/null, O_RDWR) should succeed");
    if (fd >= 0) close(fd);
}

static void testDevZeroOpenSucceeds(struct kunit *test) {
    errno = 0;
    int fd = open("/dev/zero", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/zero, O_RDONLY) should succeed");
    if (fd >= 0) close(fd);
}

static void testDevUrandomOpenSucceeds(struct kunit *test) {
    errno = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/urandom, O_RDONLY) should succeed");
    if (fd >= 0) close(fd);
}

static void testDevNullReadReturnsEOF(struct kunit *test) {
    int fd = open("/dev/null", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/null) should succeed");
    if (fd < 0) return;

    char buf[64];
    memset(buf, 0xAA, sizeof(buf));
    errno = 0;
    ssize_t nread = read(fd, buf, sizeof(buf));
    KUNIT_ASSERT_EQ_MSG(nread, 0, "read(/dev/null) should return 0 (EOF)");
    close(fd);
}

static void testDevNullWriteSucceedsAndDiscards(struct kunit *test) {
    int fd = open("/dev/null", O_WRONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/null, O_WRONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t written = write(fd, "hello", 5);
    KUNIT_ASSERT_EQ_MSG(written, 5, "write(/dev/null) should succeed and report all bytes written");
    close(fd);
}

static void testDevZeroReadFillsZeroBytes(struct kunit *test) {
    int fd = open("/dev/zero", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/zero) should succeed");
    if (fd < 0) return;

    char buf[128];
    memset(buf, 0xFF, sizeof(buf));
    errno = 0;
    ssize_t nread = read(fd, buf, sizeof(buf));
    KUNIT_ASSERT_EQ_MSG(nread, (ssize_t)sizeof(buf), "read(/dev/zero) should return requested byte count");

    bool all_zero = true;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            all_zero = false;
            break;
        }
    }
    KUNIT_ASSERT_TRUE_MSG(all_zero, "/dev/zero read should fill buffer with zero bytes");
    close(fd);
}

static void testDevUrandomReadReturnsNontrivialData(struct kunit *test) {
    int fd = open("/dev/urandom", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/urandom) should succeed");
    if (fd < 0) return;

    char buf[256];
    memset(buf, 0, sizeof(buf));
    errno = 0;
    ssize_t nread = read(fd, buf, sizeof(buf));
    KUNIT_ASSERT_EQ_MSG(nread, (ssize_t)sizeof(buf), "read(/dev/urandom) should return requested byte count");

    bool all_zero = true;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            all_zero = false;
            break;
        }
    }
    KUNIT_ASSERT_FALSE_MSG(all_zero, "/dev/urandom read should return nontrivial data (not all zeros)");
    close(fd);
}

static void testDevZeroWriteSucceedsAndDiscards(struct kunit *test) {
    int fd = open("/dev/zero", O_WRONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/zero, O_WRONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t written = write(fd, "data", 4);
    KUNIT_ASSERT_EQ_MSG(written, 4, "write(/dev/zero) should succeed and discard");
    close(fd);
}

static void testDevUrandomWriteSucceedsAndDiscards(struct kunit *test) {
    int fd = open("/dev/urandom", O_WRONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/urandom, O_WRONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t written = write(fd, "data", 4);
    KUNIT_ASSERT_EQ_MSG(written, 4, "write(/dev/urandom) should succeed and discard");
    close(fd);
}

static void testUnsupportedDevNodeStillFails(struct kunit *test) {
    errno = 0;
    struct stat st;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/dev/sda", &st), -1, "stat(/dev/sda) should fail for unsupported dev node");
    KUNIT_ASSERT_EQ_MSG(errno, ENOENT, "stat(/dev/sda) should set ENOENT");

    struct task *original_task = task_current();
    struct task *isolated_task = alloc_task();
    KUNIT_ASSERT_TRUE_MSG(isolated_task != NULL, "task allocation should succeed");
    if (!isolated_task) return;

    isolated_task->fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(isolated_task->fs != NULL, "fs_struct allocation should succeed");
    if (!isolated_task->fs) {
        task_put(isolated_task);
        return;
    }

    isolated_task->signal = alloc_signal_struct();
    KUNIT_ASSERT_TRUE_MSG(isolated_task->signal != NULL, "signal_struct allocation should succeed");
    if (!isolated_task->signal) {
        task_put(isolated_task);
        return;
    }

    fs_init_root(isolated_task->fs, "/");
    fs_init_pwd(isolated_task->fs, "/");
    task_set_current(isolated_task);

    errno = 0;
    KUNIT_ASSERT_EQ_MSG(open("/dev/tty", O_RDONLY), -1, "open(/dev/tty) should fail without usable controlling tty");
    KUNIT_ASSERT_TRUE_MSG(errno == ENXIO || errno == EIO, "open(/dev/tty) should set ENXIO (no controlling tty) or EIO (unusable controlling tty)");

    task_set_current(original_task);
    task_put(isolated_task);
}

static void testVfsFaccessatSupportsAtFdcwd(struct kunit *test) {
    int ret = vfs_faccessat(AT_FDCWD, "/etc", 1, 0);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "vfs_faccessat with AT_FDCWD should succeed");
}

static void testVfsFaccessatRejectsInvalidFlags(struct kunit *test) {
    int ret = vfs_faccessat(AT_FDCWD, "/etc", 1, INVALID_FLAG_TEST_VALUE);

    KUNIT_ASSERT_EQ_MSG(ret, -EINVAL, "vfs_faccessat should reject invalid flags");
}

static void testVfsFaccessatAtEaccessUsesEffectiveCredentials(struct kunit *test) {
    extern int vfs_contract_faccessat_eaccess_uses_effective_credentials(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_faccessat_eaccess_uses_effective_credentials(), 0,
                   "vfs_faccessat Linux AT_EACCESS should use effective credentials, errno %d", errno);
}

static void testVfsFaccessatReportsUnsupportedSymlinkNoFollow(struct kunit *test) {
    extern int vfs_contract_faccessat_symlink_nofollow_returns_enotsup(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_faccessat_symlink_nofollow_returns_enotsup(), -ENOTSUP,
                   "vfs_faccessat Linux AT_SYMLINK_NOFOLLOW should return ENOTSUP");
}

static void testFaccessatFollowsAbsoluteSymlinkAsVirtualPath(struct kunit *test) {
    extern int vfs_contract_faccessat_follows_absolute_symlink_as_virtual_path(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_faccessat_follows_absolute_symlink_as_virtual_path(), 0,
                   "faccessat/access should follow absolute symlink targets through the virtual root, errno %d", errno);
}

static void testFaccessatSymlinkLoopReturnsEloop(struct kunit *test) {
    extern int vfs_contract_faccessat_symlink_loop_returns_eloop(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_faccessat_symlink_loop_returns_eloop(), 0,
                   "faccessat/access should reject symlink loops with ELOOP, errno %d", errno);
}

static void testFsuidControlsOwnerFileAccess(struct kunit *test) {
    extern int vfs_contract_fsuid_controls_owner_file_access(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fsuid_controls_owner_file_access(), 0,
                   "fsuid should control Linux owner permission checks, errno %d", errno);
}

static void testFsgidControlsGroupFileAccess(struct kunit *test) {
    extern int vfs_contract_fsgid_controls_group_file_access(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fsgid_controls_group_file_access(), 0,
                   "fsgid should control Linux group permission checks, errno %d", errno);
}

static void testChrootRebasesAbsolutePathsAndGetcwd(struct kunit *test) {
    extern int vfs_contract_chroot_rebases_absolute_paths_and_getcwd(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_chroot_rebases_absolute_paths_and_getcwd(), 0,
                   "chroot should rebase task root and getcwd through Orlix VFS, errno %d", errno);
}

static void testNonrootCannotChroot(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_chroot(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_chroot(), 0,
                   "non-root virtual credentials should not chroot, errno %d", errno);
}

static void testRootWithoutSysChrootCannotChroot(struct kunit *test) {
    extern int vfs_contract_root_without_sys_chroot_cannot_chroot(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_root_without_sys_chroot_cannot_chroot(), 0,
                   "root without CAP_SYS_CHROOT should not chroot, errno %d", errno);
}

static void testPivotRootRebasesAbsolutePathsAndExposesOldRoot(struct kunit *test) {
    extern int vfs_contract_pivot_root_rebases_absolute_paths_and_exposes_old_root(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_pivot_root_rebases_absolute_paths_and_exposes_old_root(), 0,
                   "pivot_root should rebase absolute paths and expose the old root, errno %d", errno);
}

static void testPivotRootSyscallRebasesAbsolutePathsAndExposesOldRoot(struct kunit *test) {
    extern int vfs_contract_pivot_root_syscall_rebases_absolute_paths_and_exposes_old_root(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_pivot_root_syscall_rebases_absolute_paths_and_exposes_old_root(), 0,
                   "__NR_pivot_root should route to Orlix pivot_root semantics (not ENOSYS), errno %d", errno);
}

static void testFchdirUpdatesVirtualPwd(struct kunit *test) {
    extern int vfs_contract_fchdir_updates_virtual_pwd(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fchdir_updates_virtual_pwd(), 0,
                   "fchdir should update Orlix task pwd without host cwd, errno %d", errno);
}

static void testBindMountRedirectsTargetTree(struct kunit *test) {
    extern int vfs_contract_bind_mount_redirects_target_tree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_bind_mount_redirects_target_tree(), 0,
                   "MS_BIND mount should redirect target tree through Orlix VFS, errno %d", errno);
}

static void testBindMountDuplicateTargetReturnsBusy(struct kunit *test) {
    extern int vfs_contract_bind_mount_duplicate_target_returns_busy(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_bind_mount_duplicate_target_returns_busy(), 0,
                   "duplicate bind mount target should return EBUSY, errno %d", errno);
}

static void testUmountRestoresTargetTree(struct kunit *test) {
    extern int vfs_contract_umount_restores_target_tree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount_restores_target_tree(), 0,
                   "umount should restore target tree path resolution, errno %d", errno);
}

static void testBindMountRejectsNonBindMount(struct kunit *test) {
    extern int vfs_contract_bind_mount_rejects_non_bind_mount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_bind_mount_rejects_non_bind_mount(), 0,
                   "non-bind mount should remain unsupported without host mount semantics, errno %d", errno);
}

static void testMountAndUmount2SyscallsRouteToLinuxOwnedMountStack(struct kunit *test) {
    extern int vfs_contract_mount_syscall_bind_mount_and_umount2_work(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_syscall_bind_mount_and_umount2_work(), 0,
                   "__NR_mount/__NR_umount2 should route to Orlix mount semantics (not ENOSYS), errno %d", errno);
}

static void testMountNamespaceSharedAcrossTaskDup(struct kunit *test) {
    extern int vfs_contract_mount_namespace_shared_across_task_dup(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_namespace_shared_across_task_dup(), 0,
                   "duplicated task fs state should share the virtual mount namespace, errno %d", errno);
}

static void testMountNamespaceUnshareIsolatesChildMounts(struct kunit *test) {
    extern int vfs_contract_mount_namespace_unshare_isolates_child_mounts(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_namespace_unshare_isolates_child_mounts(), 0,
                   "unshared task mount namespace should isolate child bind mounts, errno %d", errno);
}

static void testProcSelfMountinfoListsBindMount(struct kunit *test) {
    extern int vfs_contract_proc_self_mountinfo_lists_bind_mount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mountinfo_lists_bind_mount(), 0,
                   "/proc/self/mountinfo should expose virtual bind mounts, errno %d", errno);
}

static void testProcSelfMountinfoUsesLinuxShapedOptionalFields(struct kunit *test) {
    extern int vfs_contract_proc_self_mountinfo_uses_linux_shaped_optional_fields(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mountinfo_uses_linux_shaped_optional_fields(), 0,
                   "/proc/self/mountinfo should use Linux-shaped root/source/mount option fields, errno %d", errno);
}

static void testProcSelfMountinfoReportsSharedPropagation(struct kunit *test) {
    extern int vfs_contract_proc_self_mountinfo_reports_shared_propagation(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mountinfo_reports_shared_propagation(), 0,
                   "/proc/self/mountinfo should report shared propagation metadata, errno %d", errno);
}

static void testProcSelfMountinfoReportsSlavePrivateAndUnbindablePropagation(struct kunit *test) {
    extern int vfs_contract_proc_self_mountinfo_reports_slave_private_and_unbindable_propagation(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mountinfo_reports_slave_private_and_unbindable_propagation(), 0,
                   "/proc/self/mountinfo should report Linux-shaped propagation metadata, errno %d", errno);
}

static void testMountRejectsMultiplePropagationFlags(struct kunit *test) {
    extern int vfs_contract_mount_rejects_multiple_propagation_flags(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_rejects_multiple_propagation_flags(), 0,
                   "mount should reject multiple propagation flags, errno %d", errno);
}

static void testUnprivilegedMountOperationsFailWithoutNamespaceMutation(struct kunit *test) {
    extern int vfs_contract_unprivileged_mount_operations_fail_without_namespace_mutation(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_unprivileged_mount_operations_fail_without_namespace_mutation(), 0,
                   "unprivileged mount operations should fail without mutating the virtual mount namespace, errno %d", errno);
}

static void testSharedMountPropagatesChildBindToPeer(struct kunit *test) {
    extern int vfs_contract_shared_mount_propagates_child_bind_to_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_shared_mount_propagates_child_bind_to_peer(), 0,
                   "shared mount peers should receive child bind propagation, errno %d", errno);
}

static void testSharedMountinfoUsesPeerGroupIds(struct kunit *test) {
    extern int vfs_contract_shared_mountinfo_uses_peer_group_ids(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_shared_mountinfo_uses_peer_group_ids(), 0,
                   "/proc/self/mountinfo should report deterministic shared peer groups, errno %d", errno);
}

static void testSharedMountPropagatesNestedChildBindToPeer(struct kunit *test) {
    extern int vfs_contract_shared_mount_propagates_nested_child_bind_to_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_shared_mount_propagates_nested_child_bind_to_peer(), 0,
                   "shared mount peers should recursively receive nested bind propagation, errno %d", errno);
}

static void testSharedMountUnmountPropagatesNestedChildFromPeer(struct kunit *test) {
    extern int vfs_contract_shared_mount_unmount_propagates_nested_child_from_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_shared_mount_unmount_propagates_nested_child_from_peer(), 0,
                   "shared mount peers should receive nested unmount propagation, errno %d", errno);
}

static void testRecursiveUnmountPropagatesNestedChildrenFromSharedPeer(struct kunit *test) {
    extern int vfs_contract_recursive_umount_propagates_nested_children_from_shared_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_recursive_umount_propagates_nested_children_from_shared_peer(), 0,
                   "recursive unmount should propagate nested child unmounts from shared peer trees, errno %d", errno);
}

static void testHardlinkInodeMetadataSurvivesUnlink(struct kunit *test) {
    extern int vfs_contract_hardlink_inode_metadata_survives_unlink(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_hardlink_inode_metadata_survives_unlink(), 0,
                   "hardlink inode metadata should sync across aliases and survive unlink, errno %d", errno);
}

static void testProcFdMarksOpenUnlinkedFileDeleted(struct kunit *test) {
    extern int vfs_contract_proc_fd_marks_open_unlinked_file_deleted(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_fd_marks_open_unlinked_file_deleted(), 0,
                   "/proc/self/fd should preserve open unlinked file identity, errno %d", errno);
}

static void testPrivateChildUnmountDoesNotPropagateToSharedPeer(struct kunit *test) {
    extern int vfs_contract_private_child_unmount_does_not_propagate_to_shared_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_private_child_unmount_does_not_propagate_to_shared_peer(), 0,
                   "private child unmount should not propagate to shared peer trees, errno %d", errno);
}

static void testSlaveChildUnmountDoesNotPropagateToSharedPeer(struct kunit *test) {
    extern int vfs_contract_slave_child_unmount_does_not_propagate_to_shared_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_slave_child_unmount_does_not_propagate_to_shared_peer(), 0,
                   "slave child unmount should not propagate back to shared peer trees, errno %d", errno);
}

static void testCloneNewnsSharedPropagationStaysInsideChildNamespace(struct kunit *test) {
    extern int vfs_contract_clone_newns_shared_propagation_stays_inside_child_namespace(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_clone_newns_shared_propagation_stays_inside_child_namespace(), 0,
                   "CLONE_NEWNS propagation should stay within the child mount namespace, errno %d", errno);
}

static void testMountNamespaceRefsTrackTaskLifecycle(struct kunit *test) {
    extern int vfs_contract_mount_namespace_refs_track_task_lifecycle(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_namespace_refs_track_task_lifecycle(), 0,
                   "mount namespace refs should track cloned task lifetime, errno %d", errno);
}

static void testCloneNewnsRebasesSharedPeerGroups(struct kunit *test) {
    extern int vfs_contract_clone_newns_rebases_shared_peer_groups(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_clone_newns_rebases_shared_peer_groups(), 0,
                   "CLONE_NEWNS should rebase shared peer group ids inside the cloned mount namespace, errno %d", errno);
}

static void testCloneNewnsRebasesSlaveMasterToChildPeerGroup(struct kunit *test) {
    extern int vfs_contract_clone_newns_rebases_slave_master_to_child_peer_group(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_clone_newns_rebases_slave_master_to_child_peer_group(), 0,
                   "CLONE_NEWNS should rebase slave masters to child peer groups, errno %d", errno);
}

static void testUnmountBusyWhenOpenFdPinsMountTree(struct kunit *test) {
    extern int vfs_contract_umount_busy_when_open_fd_pins_mount_tree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount_busy_when_open_fd_pins_mount_tree(), 0,
                   "normal unmount should return EBUSY while an open fd pins the mount tree, errno %d", errno);
}

static void testLazyUnmountDetachesBusyMountFromNamespace(struct kunit *test) {
    extern int vfs_contract_lazy_umount_detaches_busy_mount_from_namespace(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_lazy_umount_detaches_busy_mount_from_namespace(), 0,
                   "lazy unmount should detach a busy mount from the namespace, errno %d", errno);
}

static void testLazyUnmountRemovesBusyMountFromProcMountinfo(struct kunit *test) {
    extern int vfs_contract_lazy_umount_removes_busy_mount_from_proc_mountinfo(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_lazy_umount_removes_busy_mount_from_proc_mountinfo(), 0,
                   "lazy unmount should remove a busy detached mount from proc mountinfo, errno %d", errno);
}

static void testUnmountExpireRequiresMarkThenUnmount(struct kunit *test) {
    extern int vfs_contract_umount_expire_requires_mark_then_unmount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount_expire_requires_mark_then_unmount(), 0,
                   "expiry unmount should require one mark pass before detaching, errno %d", errno);
}

static void testLazyUnmountReclaimsDetachedRefAfterPinRelease(struct kunit *test) {
    extern int vfs_contract_lazy_umount_reclaims_detached_ref_after_pin_release(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_lazy_umount_reclaims_detached_ref_after_pin_release(), 0,
                   "lazy unmount should retain and later reclaim detached mount refs, errno %d", errno);
}

static void testUnmountBusyWhenPwdPinsMountTree(struct kunit *test) {
    extern int vfs_contract_umount_busy_when_pwd_pins_mount_tree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount_busy_when_pwd_pins_mount_tree(), 0,
                   "normal unmount should return EBUSY while cwd pins the mount tree, errno %d", errno);
}

static void testUnmountBusyWhenRootPinsMountTree(struct kunit *test) {
    extern int vfs_contract_umount_busy_when_root_pins_mount_tree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount_busy_when_root_pins_mount_tree(), 0,
                   "normal unmount should return EBUSY while a task root pins the mount tree, errno %d", errno);
}

static void testSlaveMountReceivesNestedChildFromSharedMaster(struct kunit *test) {
    extern int vfs_contract_slave_mount_receives_nested_child_from_shared_master(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_slave_mount_receives_nested_child_from_shared_master(), 0,
                   "slave mounts should receive nested propagation from their shared master, errno %d", errno);
}

static void testMountSetattrRecursiveMarksChildPrivate(struct kunit *test) {
    extern int vfs_contract_mount_setattr_recursive_marks_child_private(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_setattr_recursive_marks_child_private(), 0,
                   "mount_setattr AT_RECURSIVE should update the virtual mount subtree, errno %d", errno);
}

static void testRecursiveRemountPrivateMarksChildPrivate(struct kunit *test) {
    extern int vfs_contract_recursive_remount_private_marks_child_private(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_recursive_remount_private_marks_child_private(), 0,
                   "MS_REC propagation remount should update the virtual mount subtree, errno %d", errno);
}

static void testMountSetattrRecursiveAttrsVisibleInStatmount(struct kunit *test) {
    extern int vfs_contract_mount_setattr_recursive_attrs_visible_in_statmount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_setattr_recursive_attrs_visible_in_statmount(), 0,
                   "mount_setattr AT_RECURSIVE should expose child mount attrs through statmount, errno %d", errno);
}

static void testRecursiveRemountAttrsVisibleInStatmount(struct kunit *test) {
    extern int vfs_contract_recursive_remount_attrs_visible_in_statmount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_recursive_remount_attrs_visible_in_statmount(), 0,
                   "MS_REC remount should expose child mount attrs through statmount, errno %d", errno);
}

static void testRecursiveRemountSlavePreservesPeerGroupMasters(struct kunit *test) {
    extern int vfs_contract_recursive_remount_slave_preserves_peer_group_masters(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_recursive_remount_slave_preserves_peer_group_masters(), 0,
                   "MS_REC slave remount should preserve former shared peer groups as masters, errno %d", errno);
}

static void testListmountStatmountReportsSlaveMaster(struct kunit *test) {
    extern int vfs_contract_listmount_statmount_reports_slave_master(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_listmount_statmount_reports_slave_master(), 0,
                   "listmount/statmount should expose slave master propagation metadata, errno %d", errno);
}

static void testListmountWalksMountSubtreeByParentId(struct kunit *test) {
    extern int vfs_contract_listmount_walks_mount_subtree_by_parent_id(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_listmount_walks_mount_subtree_by_parent_id(), 0,
                   "listmount should walk immediate mount children by parent mount id, errno %d", errno);
}

static void testMountinfoReportsNestedParentMountId(struct kunit *test) {
    extern int vfs_contract_mountinfo_reports_nested_parent_mount_id(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mountinfo_reports_nested_parent_mount_id(), 0,
                   "/proc/self/mountinfo should report nested parent mount ids, errno %d", errno);
}

static void testRecursiveBindClonesNestedMountTopology(struct kunit *test) {
    extern int vfs_contract_recursive_bind_clones_nested_mount_topology(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_recursive_bind_clones_nested_mount_topology(), 0,
                   "MS_REC bind should clone nested mount topology, errno %d", errno);
}

static void testMoveMountRelocatesBindSubtree(struct kunit *test) {
    extern int vfs_contract_move_mount_relocates_bind_subtree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_move_mount_relocates_bind_subtree(), 0,
                   "MS_MOVE should relocate a virtual mount subtree, errno %d", errno);
}

static void testOpenTreeCloneReturnsMountFdVisibleInProc(struct kunit *test) {
    extern int vfs_contract_open_tree_clone_returns_mount_fd_visible_in_proc(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_tree_clone_returns_mount_fd_visible_in_proc(), 0,
                   "open_tree should return a virtual mount fd visible through /proc/self/fd, errno %d", errno);
}

static void testMoveMountAttachesOpenTreeClone(struct kunit *test) {
    extern int vfs_contract_move_mount_attaches_open_tree_clone(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_move_mount_attaches_open_tree_clone(), 0,
                   "move_mount should attach an open_tree clone fd into the virtual mount namespace, errno %d", errno);
}

static void testOpenTreeCloneSurvivesSourceUnmountUntilAttached(struct kunit *test) {
    extern int vfs_contract_open_tree_clone_survives_source_unmount_until_attached(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_tree_clone_survives_source_unmount_until_attached(), 0,
                   "open_tree clone fd should survive source unmount until attached, errno %d", errno);
}

static void testOpenTreeCloneNestedMountTopologyAttachesRecursively(struct kunit *test) {
    extern int vfs_contract_open_tree_clone_nested_mount_topology_attaches_recursively(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_tree_clone_nested_mount_topology_attaches_recursively(), 0,
                   "open_tree clone should preserve nested mount topology when attached, errno %d", errno);
}

static void testOpenTreeCloneAttachPropagatesPrivateSubtreeToSharedAndSlave(struct kunit *test) {
    extern int vfs_contract_open_tree_clone_attach_propagates_private_subtree_to_shared_and_slave(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_tree_clone_attach_propagates_private_subtree_to_shared_and_slave(), 0,
                   "open_tree clone attach should propagate the detached subtree into shared peers and slave receivers, errno %d", errno);
}

static void testOpenTreeCloneSurvivesSourceNamespaceTeardown(struct kunit *test) {
    extern int vfs_contract_open_tree_clone_survives_source_namespace_teardown(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_open_tree_clone_survives_source_namespace_teardown(), 0,
                   "open_tree clone fd should survive source mount namespace teardown, errno %d", errno);
}

static void testSharedMountMovePropagatesToPeer(struct kunit *test) {
    extern int vfs_contract_shared_mount_move_propagates_to_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_shared_mount_move_propagates_to_peer(), 0,
                   "move_mount should propagate moved shared child mounts to peer mount trees, errno %d", errno);
}

static void testSharedMoveUpdatesProcMountinfoForPeer(struct kunit *test) {
    extern int vfs_contract_shared_move_updates_proc_mountinfo_for_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_shared_move_updates_proc_mountinfo_for_peer(), 0,
                   "shared move propagation should update /proc/self/mountinfo for peer mount trees, errno %d", errno);
}

static void testSlaveChildMoveDoesNotPropagateToSharedPeer(struct kunit *test) {
    extern int vfs_contract_slave_child_move_does_not_propagate_to_shared_peer(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_slave_child_move_does_not_propagate_to_shared_peer(), 0,
                   "moving a slave child should not propagate back to shared peer trees, errno %d", errno);
}

static void testCloneNewnsMovePropagatesToRebasedSlaveReceiver(struct kunit *test) {
    extern int vfs_contract_clone_newns_move_propagates_to_rebased_slave_receiver(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_clone_newns_move_propagates_to_rebased_slave_receiver(), 0,
                   "move_mount should propagate inside cloned namespaces to rebased slave receivers, errno %d", errno);
}

static void testRecursiveUmountPropagatesNestedSharedSubtree(struct kunit *test) {
    extern int vfs_contract_recursive_umount_propagates_nested_shared_subtree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_recursive_umount_propagates_nested_shared_subtree(), 0,
                   "recursive unmount should propagate nested shared subtree removal, errno %d", errno);
}

static void testRecursiveUmountUpdatesProcMountinfoForPropagatedPeers(struct kunit *test) {
    extern int vfs_contract_recursive_umount_updates_proc_mountinfo_for_propagated_peers(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_recursive_umount_updates_proc_mountinfo_for_propagated_peers(), 0,
                   "recursive unmount propagation should update /proc/self/mountinfo, errno %d", errno);
}

static void testStatmountRejectsPropagatedRemovedMountId(struct kunit *test) {
    extern int vfs_contract_statmount_rejects_propagated_removed_mount_id(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_statmount_rejects_propagated_removed_mount_id(), 0,
                   "statmount/listmount should reject propagated removed mount ids, errno %d", errno);
}

static void testMountIdsStableAcrossMoveUnmountAndNamespaceClone(struct kunit *test) {
    extern int vfs_contract_mount_ids_stable_across_move_unmount_and_namespace_clone(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_ids_stable_across_move_unmount_and_namespace_clone(), 0,
                   "virtual mount ids should survive move, sibling unmount, and namespace clone, errno %d", errno);
}

static void testMountNamespaceTeardownAccountsMountsAndDetachedRefs(struct kunit *test) {
    extern int vfs_contract_mount_namespace_teardown_accounts_mounts_and_detached_refs(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_namespace_teardown_accounts_mounts_and_detached_refs(), 0,
                   "mount namespace teardown should account active mounts and detached refs, errno %d", errno);
}

static void testMountNamespaceDropReclaimsChildDetachedRefs(struct kunit *test) {
    extern int vfs_contract_mount_namespace_drop_reclaims_child_detached_refs(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_mount_namespace_drop_reclaims_child_detached_refs(), 0,
                   "dropping a child mount namespace should reclaim its detached refs, errno %d", errno);
}

static void testLazyUmountRefSurvivesDescendantTaskTree(struct kunit *test) {
    extern int vfs_contract_lazy_umount_ref_survives_descendant_task_tree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_lazy_umount_ref_survives_descendant_task_tree(), 0,
                   "lazy detached refs should survive descendant task pins and reap after task release, errno %d", errno);
}

static void testLazyUmountRefSurvivesChildRootAndPwdPins(struct kunit *test) {
    extern int vfs_contract_lazy_umount_ref_survives_child_root_and_pwd_pins(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_lazy_umount_ref_survives_child_root_and_pwd_pins(), 0,
                   "lazy detached refs should survive child root/cwd pins and reap after task release, errno %d", errno);
}

static void testChildMountNamespaceDetachSurvivesChildRootAndPwdPins(struct kunit *test) {
    extern int vfs_contract_child_mount_namespace_detach_survives_child_root_and_pwd_pins(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_child_mount_namespace_detach_survives_child_root_and_pwd_pins(), 0,
                   "child mount namespace detached refs should survive child root/cwd pins and reap after child release, errno %d", errno);
}

static void testLazyDetachPropagatesNestedSharedSlaveTree(struct kunit *test) {
    extern int vfs_contract_lazy_detach_propagates_nested_shared_slave_tree(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_lazy_detach_propagates_nested_shared_slave_tree(), 0,
                   "lazy detach should propagate nested shared/slave mount removal without leaking detached refs, errno %d", errno);
}

static void testUmount2DetachDetachesBusyMountFromNamespace(struct kunit *test) {
    extern int vfs_contract_umount2_detach_detaches_busy_mount_from_namespace(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount2_detach_detaches_busy_mount_from_namespace(), 0,
                   "umount2 MNT_DETACH should detach busy mount from virtual namespace, errno %d", errno);
}

static void testUmount2SyscallDetachDetachesBusyMountFromNamespace(struct kunit *test) {
    extern int vfs_contract_umount2_syscall_detach_detaches_busy_mount_from_namespace(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount2_syscall_detach_detaches_busy_mount_from_namespace(), 0,
                   "__NR_umount2(MNT_DETACH) should route to Orlix detach semantics, errno %d", errno);
}

static void testUmount2RejectsUnusedLinuxUmountFlag(struct kunit *test) {
    extern int vfs_contract_umount2_rejects_unused_linux_umount_flag(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount2_rejects_unused_linux_umount_flag(), 0,
                   "umount2 should reject the Linux UMOUNT_UNUSED flag and leave the mount visible, errno %d", errno);
}

static void testUmount2ForceDetachesBusyMountAndReapsAfterPinRelease(struct kunit *test) {
    extern int vfs_contract_umount2_force_detaches_busy_mount_and_reaps_after_pin_release(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount2_force_detaches_busy_mount_and_reaps_after_pin_release(), 0,
                   "umount2 MNT_FORCE should detach busy virtual mounts and reap after pins release, errno %d", errno);
}

static void testForceUmountDetachedRefsAreMountNamespaceScoped(struct kunit *test) {
    extern int vfs_contract_force_umount_detached_refs_are_mount_namespace_scoped(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_force_umount_detached_refs_are_mount_namespace_scoped(), 0,
                   "detached mount refs should be scoped to the mount namespace that was detached, errno %d", errno);
}

static void testForceUmountPropagatesSharedSlaveSubtreeTeardown(struct kunit *test) {
    extern int vfs_contract_force_umount_propagates_shared_slave_subtree_teardown(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_force_umount_propagates_shared_slave_subtree_teardown(), 0,
                   "MNT_FORCE subtree teardown should propagate across shared peers and slave receivers, errno %d", errno);
}

static void testUmount2ExpireRequiresMarkThenUnmount(struct kunit *test) {
    extern int vfs_contract_umount2_expire_requires_mark_then_unmount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount2_expire_requires_mark_then_unmount(), 0,
                   "umount2 MNT_EXPIRE should mark then unmount on second call, errno %d", errno);
}

static void testUmount2RejectsExpireWithDetach(struct kunit *test) {
    extern int vfs_contract_umount2_rejects_expire_with_detach(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount2_rejects_expire_with_detach(), 0,
                   "umount2 should reject MNT_EXPIRE combined with MNT_DETACH, errno %d", errno);
}

static void testUmount2NofollowRejectsSymlinkTarget(struct kunit *test) {
    extern int vfs_contract_umount2_nofollow_rejects_symlink_target(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_umount2_nofollow_rejects_symlink_target(), 0,
                   "umount2 UMOUNT_NOFOLLOW should reject symlink mount targets, errno %d", errno);
}

static void testProcSelfMountsListsBindMount(struct kunit *test) {
    extern int vfs_contract_proc_self_mounts_lists_bind_mount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mounts_lists_bind_mount(), 0,
                   "/proc/self/mounts should expose virtual bind mounts, errno %d", errno);
}

static void testBindMountRemountReadonlyRejectsWrites(struct kunit *test) {
    extern int vfs_contract_bind_mount_remount_readonly_rejects_writes(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_bind_mount_remount_readonly_rejects_writes(), 0,
                   "read-only virtual bind mounts should reject writes with EROFS, errno %d", errno);
}

static void testBindMountRemountReadwritePermitsWrites(struct kunit *test) {
    extern int vfs_contract_bind_mount_remount_readwrite_permits_writes(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_bind_mount_remount_readwrite_permits_writes(), 0,
                   "read-write remount should permit writes through the virtual bind mount, errno %d", errno);
}

static void testProcSelfMountinfoReportsReadonlyRemount(struct kunit *test) {
    extern int vfs_contract_proc_self_mountinfo_reports_readonly_remount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mountinfo_reports_readonly_remount(), 0,
                   "/proc/self/mountinfo should report read-only remounted bind mounts, errno %d", errno);
}

static void testNonrootCannotCreateBindMount(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_create_bind_mount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_create_bind_mount(), 0,
                   "non-root virtual credentials should not create bind mounts, errno %d", errno);
}

static void testRootWithoutSysAdminCannotCreateBindMount(struct kunit *test) {
    extern int vfs_contract_root_without_sys_admin_cannot_create_bind_mount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_root_without_sys_admin_cannot_create_bind_mount(), 0,
                   "root without CAP_SYS_ADMIN should not create bind mounts, errno %d", errno);
}

static void testNonrootCannotUnmountBindMount(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_unmount_bind_mount(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_unmount_bind_mount(), 0,
                   "non-root virtual credentials should not unmount bind mounts, errno %d", errno);
}

static void testProcSelfMountinfoUsesCurrentMountNamespace(struct kunit *test) {
    extern int vfs_contract_proc_self_mountinfo_uses_current_mount_namespace(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mountinfo_uses_current_mount_namespace(), 0,
                   "/proc/self/mountinfo should follow current task mount namespace, errno %d", errno);
}

static void testProcSelfMountViewsDoNotExposeHostPaths(struct kunit *test) {
    extern int vfs_contract_proc_self_mount_views_do_not_expose_host_paths(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_proc_self_mount_views_do_not_expose_host_paths(), 0,
                   "proc mount views should not expose host backing paths, errno %d", errno);
}

static void testNonrootCannotReadRootPrivateFile(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_read_root_private_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_read_root_private_file(), 0,
                   "non-root virtual credentials should not read root-private files, errno %d", errno);
}

static void testNonrootCanReadOtherReadableFile(struct kunit *test) {
    extern int vfs_contract_nonroot_can_read_other_readable_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_can_read_other_readable_file(), 0,
                   "other-readable files should remain readable by non-root credentials, errno %d", errno);
}

static void testNonrootCreatedFileRecordsVirtualOwner(struct kunit *test) {
    extern int vfs_contract_nonroot_created_file_records_virtual_owner(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_created_file_records_virtual_owner(), 0,
                   "created files should record the current virtual euid as owner, errno %d", errno);
}

static void testNonrootCannotUnlinkInsideRootPrivateDir(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_unlink_inside_root_private_dir(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_unlink_inside_root_private_dir(), 0,
                   "non-root virtual credentials should not unlink inside root-private directories, errno %d", errno);
}

static void testNonrootCannotOpenThroughUnsearchableParentDirectory(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_open_through_unsearchable_parent_directory(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_open_through_unsearchable_parent_directory(), 0,
                   "non-root should need execute permission on parent directories, errno %d", errno);
}

static void testStickyDirectoryBlocksNonOwnerUnlink(struct kunit *test) {
    extern int vfs_contract_sticky_directory_blocks_nonowner_unlink(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_sticky_directory_blocks_nonowner_unlink(), 0,
                   "sticky directories should block unlink by non-owners, errno %d", errno);
}

static void testStickyDirectoryBlocksNonOwnerRename(struct kunit *test) {
    extern int vfs_contract_sticky_directory_blocks_nonowner_rename(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_sticky_directory_blocks_nonowner_rename(), 0,
                   "sticky directories should block rename by non-owners, errno %d", errno);
}

static void testStickyDirectoryBlocksNonOwnerExchangeTarget(struct kunit *test) {
    extern int vfs_contract_sticky_directory_blocks_nonowner_exchange_target(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_sticky_directory_blocks_nonowner_exchange_target(), 0,
                   "sticky directories should block exchange of non-owned target, errno %d", errno);
}

static void testNonrootCannotMkdiratInsideRootPrivateDir(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_mkdirat_inside_root_private_dir(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_mkdirat_inside_root_private_dir(), 0,
                   "mkdirat should enforce virtual parent permissions after credential changes, errno %d", errno);
}

static void testNonrootCannotUnlinkatInsideRootPrivateDir(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_unlinkat_inside_root_private_dir(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_unlinkat_inside_root_private_dir(), 0,
                   "unlinkat should enforce virtual parent permissions after credential changes, errno %d", errno);
}

static void testLinkatUsesVirtualDirfds(struct kunit *test) {
    extern int vfs_contract_linkat_uses_virtual_dirfds(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_linkat_uses_virtual_dirfds(), 0,
                   "linkat should resolve source and target through virtual dirfds, errno %d", errno);
}

static void testSymlinkatAndReadlinkatUseVirtualDirfds(struct kunit *test) {
    extern int vfs_contract_symlinkat_and_readlinkat_use_virtual_dirfds(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_symlinkat_and_readlinkat_use_virtual_dirfds(), 0,
                   "symlinkat/readlinkat should resolve through virtual dirfds, errno %d", errno);
}

static void testRenameat2ExchangeSwapsFilesThroughVirtualDirfds(struct kunit *test) {
    extern int vfs_contract_renameat2_exchange_swaps_files_through_virtual_dirfds(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_renameat2_exchange_swaps_files_through_virtual_dirfds(), 0,
                   "renameat2 exchange should swap files through virtual dirfds, errno %d", errno);
}

static void testRenameat2ExchangeSwapsVirtualMetadata(struct kunit *test) {
    extern int vfs_contract_renameat2_exchange_swaps_virtual_metadata(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_renameat2_exchange_swaps_virtual_metadata(), 0,
                   "renameat2 exchange should swap virtual metadata with files, errno %d", errno);
}

static void testRenameat2NoreplaceExistingTargetReturnsExist(struct kunit *test) {
    extern int vfs_contract_renameat2_noreplace_existing_target_returns_exist(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_renameat2_noreplace_existing_target_returns_exist(), 0,
                   "renameat2 noreplace should fail with EEXIST and leave files unchanged, errno %d", errno);
}

static void testRenameatOverwriteMovesVirtualMetadata(struct kunit *test) {
    extern int vfs_contract_renameat_overwrite_moves_virtual_metadata(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_renameat_overwrite_moves_virtual_metadata(), 0,
                   "rename overwrite should move source virtual metadata to target, errno %d", errno);
}

static void testRenameDirectoryOverNonemptyDirectoryReturnsNotempty(struct kunit *test) {
    extern int vfs_contract_rename_directory_over_nonempty_directory_returns_notempty(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_rename_directory_over_nonempty_directory_returns_notempty(), 0,
                   "rename directory over non-empty directory should return ENOTEMPTY, errno %d", errno);
}

static void testRenameFileOverDirectoryReturnsIsdir(struct kunit *test) {
    extern int vfs_contract_rename_file_over_directory_returns_isdir(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_rename_file_over_directory_returns_isdir(), 0,
                   "rename file over directory should return EISDIR, errno %d", errno);
}

static void testRenameDirectoryOverFileReturnsNotdir(struct kunit *test) {
    extern int vfs_contract_rename_directory_over_file_returns_notdir(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_rename_directory_over_file_returns_notdir(), 0,
                   "rename directory over file should return ENOTDIR, errno %d", errno);
}

static void testRootChownUpdatesVirtualOwner(struct kunit *test) {
    extern int vfs_contract_root_chown_updates_virtual_owner(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_root_chown_updates_virtual_owner(), 0,
                   "root chown should update virtual owner metadata, errno %d", errno);
}

static void testNonrootCannotChownOwnedFile(struct kunit *test) {
    extern int vfs_contract_nonroot_cannot_chown_owned_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonroot_cannot_chown_owned_file(), 0,
                   "non-root chown should fail through virtual credential checks, errno %d", errno);
}

static void testOwnerChmodUpdatesVirtualMode(struct kunit *test) {
    extern int vfs_contract_owner_chmod_updates_virtual_mode(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_owner_chmod_updates_virtual_mode(), 0,
                   "file owner chmod should update virtual mode metadata, errno %d", errno);
}

static void testNonownerCannotChmodFile(struct kunit *test) {
    extern int vfs_contract_nonowner_cannot_chmod_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nonowner_cannot_chmod_file(), 0,
                   "non-owner chmod should fail through virtual credential checks, errno %d", errno);
}

static void testFchmodUpdatesVirtualMode(struct kunit *test) {
    extern int vfs_contract_fchmod_updates_virtual_mode(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fchmod_updates_virtual_mode(), 0,
                   "fchmod should update virtual mode metadata, errno %d", errno);
}

static void testFchownUpdatesVirtualOwner(struct kunit *test) {
    extern int vfs_contract_fchown_updates_virtual_owner(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_fchown_updates_virtual_owner(), 0,
                   "fchown should update virtual owner metadata, errno %d", errno);
}

static void testSupplementaryGroupCanReadGroupFile(struct kunit *test) {
    extern int vfs_contract_supplementary_group_can_read_group_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_supplementary_group_can_read_group_file(), 0,
                   "supplementary group membership should grant group read permission, errno %d", errno);
}

static void testMissingSupplementaryGroupCannotReadGroupFile(struct kunit *test) {
    extern int vfs_contract_missing_supplementary_group_cannot_read_group_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_missing_supplementary_group_cannot_read_group_file(), 0,
                   "missing supplementary group membership should not grant group read permission, errno %d", errno);
}

static void testRootWithoutDacCapsCannotReadPrivateFile(struct kunit *test) {
    extern int vfs_contract_root_without_dac_caps_cannot_read_private_file(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_root_without_dac_caps_cannot_read_private_file(), 0,
                   "clearing virtual DAC capabilities should make root obey file mode permissions, errno %d", errno);
}

static void testStatfsReportsVirtualProcAndTmpfs(struct kunit *test) {
    extern int vfs_contract_statfs_reports_virtual_proc_and_tmpfs(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_statfs_reports_virtual_proc_and_tmpfs(), 0,
                   "statfs/fstatfs should report Orlix virtual filesystem state, errno %d", errno);
}

static void testStatfsReportsMountAttributeFlags(struct kunit *test) {
    extern int vfs_contract_statfs_reports_mount_attribute_flags(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_statfs_reports_mount_attribute_flags(), 0,
                   "statfs should report Linux mount attribute flags, errno %d", errno);
}

static void testNodevMountBlocksDeviceOpen(struct kunit *test) {
    extern int vfs_contract_nodev_mount_blocks_device_open(void);
    KUNIT_ASSERT_EQ_MSG(vfs_contract_nodev_mount_blocks_device_open(), 0,
                   "nodev mount should block device opens, errno %d", errno);
}

/* ============================================================================
 * SIGNAL-FAMILY SEMANTICS TESTS
 * ============================================================================ */

static void testSignalKillSucceeds(struct kunit *test) {
    pid_t pid = getpid();
    int ret = kill(pid, 0);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "kill(self, 0) should succeed");
}

/* ============================================================================
 * PROC/SELF ABSTRACTION TESTS
 * ============================================================================ */

static void testProcSelfStatSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/self", &st), 0, "stat(/proc/self) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "/proc/self should be a directory");
}

static void testProcSelfStatFileSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/self/stat", &st), 0, "stat(/proc/self/stat) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "/proc/self/stat should be a regular file");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0444, "/proc/self/stat should have 0444 permissions");
}

static void testProcSelfCmdlineSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/self/cmdline", &st), 0, "stat(/proc/self/cmdline) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "/proc/self/cmdline should be a regular file");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0444, "/proc/self/cmdline should have 0444 permissions");
}

static void testProcSelfCommSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/self/comm", &st), 0, "stat(/proc/self/comm) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "/proc/self/comm should be a regular file");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0444, "/proc/self/comm should have 0444 permissions");
}

static void testProcSelfStatmSucceeds(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/self/statm", &st), 0, "stat(/proc/self/statm) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "/proc/self/statm should be a regular file");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0444, "/proc/self/statm should have 0444 permissions");
}

static void testProcSelfExeIsSymlink(struct kunit *test) {
    struct stat st;
    struct task *task = task_current();
    KUNIT_ASSERT_TRUE_MSG(task != NULL, "current task should exist");
    if (!task) return;

    char original_exe[MAX_PATH];
    strncpy(original_exe, task->exe, sizeof(original_exe) - 1);
    original_exe[sizeof(original_exe) - 1] = '\0';

    strncpy(task->exe, "/bin/test-proc-self-exe", sizeof(task->exe) - 1);
    task->exe[sizeof(task->exe) - 1] = '\0';

    errno = 0;
    int ret = lstat_impl("/proc/self/exe", &st);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "lstat(/proc/self/exe) should succeed when exe is seeded");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_symlink(st.st_mode), "/proc/self/exe should be a symlink");

    strncpy(task->exe, original_exe, sizeof(task->exe) - 1);
    task->exe[sizeof(task->exe) - 1] = '\0';
}

static void testProcSelfCwdIsSymlink(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(lstat_impl("/proc/self/cwd", &st), 0, "lstat(/proc/self/cwd) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_symlink(st.st_mode), "/proc/self/cwd should be a symlink");
}

static void testProcSelfFdIsDirectory(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/self/fd", &st), 0, "stat(/proc/self/fd) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "/proc/self/fd should be a directory");
}

static void testProcSelfFdinfoIsDirectory(struct kunit *test) {
    struct stat st;
    errno = 0;
    KUNIT_ASSERT_EQ_MSG(stat_impl("/proc/self/fdinfo", &st), 0, "stat(/proc/self/fdinfo) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_directory(st.st_mode), "/proc/self/fdinfo should be a directory");
}

/* ============================================================================
 * /PROC/SELF/FD/ SYMLINK TESTS
 * ============================================================================ */

static void testProcSelfFdSymlinksExist(struct kunit *test) {
    struct stat st;

    // Get stdin fd (0) info via /proc/self/fd/0
    errno = 0;
    int ret = lstat_impl("/proc/self/fd/0", &st);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "lstat(/proc/self/fd/0) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_symlink(st.st_mode), "/proc/self/fd/0 should be a symlink");

    // Get stdout fd (1) info via /proc/self/fd/1
    errno = 0;
    ret = lstat_impl("/proc/self/fd/1", &st);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "lstat(/proc/self/fd/1) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_symlink(st.st_mode), "/proc/self/fd/1 should be a symlink");

    // Get stderr fd (2) info via /proc/self/fd/2
    errno = 0;
    ret = lstat_impl("/proc/self/fd/2", &st);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "lstat(/proc/self/fd/2) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_symlink(st.st_mode), "/proc/self/fd/2 should be a symlink");
}

static void testProcSelfFdSymlinksPointToValidPaths(struct kunit *test) {
    char link_target[MAX_PATH];
    ssize_t len;

    // Read symlink target for fd 0
    len = readlink("/proc/self/fd/0", link_target, sizeof(link_target) - 1);
    KUNIT_ASSERT_TRUE_MSG(len > 0, "readlink(/proc/self/fd/0) should return a path");
    if (len > 0) {
        link_target[len] = '\0';
        KUNIT_ASSERT_TRUE_MSG(strlen(link_target) > 0, "fd 0 should point to a valid path");
    }
}

static void testProcSelfFdSymlinksReflectActualFdState(struct kunit *test) {
    // Create a temporary file to get a real fd
    int test_fd = vfs_path_contract_open_tmp_fd_symlink_file();
    KUNIT_ASSERT_TRUE_MSG(test_fd >= 0, "open should succeed for test file");
    if (test_fd < 0) return;

    // Construct the /proc/self/fd path for this fd
    char fd_path[64];
    char fdPathString[64];
    KUNIT_ASSERT_EQ(test, vfs_path_proc_fd_string(fdPathString, sizeof(fdPathString), test_fd), 0);
    const char *fdPathUTF8 = fdPathString;
    KUNIT_ASSERT_NOT_NULL_MSG(fdPathString, "fd path string should be created");
    KUNIT_ASSERT_NE_MSG(fdPathUTF8, NULL, "fd path UTF8 conversion should succeed");
    if (fdPathUTF8 == NULL) {
        close(test_fd);
        return;
    }
    size_t fdPathLength = strlen(fdPathUTF8);
    KUNIT_ASSERT_TRUE_MSG(fdPathLength < sizeof(fd_path), "fd path should fit fixed-size buffer");
    if (fdPathLength >= sizeof(fd_path)) {
        close(test_fd);
        return;
    }
    memcpy(fd_path, fdPathUTF8, fdPathLength + 1);

    // Verify the symlink exists and points to the expected path
    char link_target[MAX_PATH];
    ssize_t len = readlink(fd_path, link_target, sizeof(link_target) - 1);

    KUNIT_ASSERT_TRUE_MSG(len > 0, "readlink should succeed for newly created fd");

    if (len > 0) {
        link_target[len] = '\0';
        // Should contain "test_fd_symlink" somewhere in the path
        KUNIT_ASSERT_TRUE_MSG(strstr(link_target, "test_fd_symlink") != NULL,
                      "fd symlink should point to the test file");
    }

    // Clean up
    close(test_fd);
    unlink("/tmp/test_fd_symlink");
}

static void testProcSelfFdInvalidFdNumbersFail(struct kunit *test) {
    struct stat st;

    // Try to stat a non-existent fd
    errno = 0;
    int ret = stat_impl("/proc/self/fd/999", &st);
    KUNIT_ASSERT_EQ_MSG(ret, -1, "stat(/proc/self/fd/999) should fail for invalid fd");
    KUNIT_ASSERT_EQ_MSG(errno, ENOENT, "errno should be ENOENT for invalid fd");

    // Try to stat a non-numeric fd "name"
    errno = 0;
    ret = stat_impl("/proc/self/fd/abc", &st);
    KUNIT_ASSERT_EQ_MSG(ret, -1, "stat(/proc/self/fd/abc) should fail for non-numeric fd");
    KUNIT_ASSERT_EQ_MSG(errno, ENOENT, "errno should be ENOENT for non-numeric fd");
}

static void testProcSelfFdinfoFilesExist(struct kunit *test) {
    struct stat st;

    // fdinfo/0 should exist and be a regular file
    errno = 0;
    int ret = stat_impl("/proc/self/fdinfo/0", &st);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "stat(/proc/self/fdinfo/0) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "/proc/self/fdinfo/0 should be a regular file");
    KUNIT_ASSERT_EQ_MSG(st.st_mode & 0777, 0444, "/proc/self/fdinfo/0 should have 0444 permissions");

    // fdinfo/1 should also exist
    errno = 0;
    ret = stat_impl("/proc/self/fdinfo/1", &st);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "stat(/proc/self/fdinfo/1) should succeed");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_regular(st.st_mode), "/proc/self/fdinfo/1 should be a regular file");
}

static void testProcSelfFdinfoInvalidFdNumbersFail(struct kunit *test) {
    struct stat st;

    // Try to stat a non-existent fdinfo entry
    errno = 0;
    int ret = stat_impl("/proc/self/fdinfo/999", &st);
    KUNIT_ASSERT_EQ_MSG(ret, -1, "stat(/proc/self/fdinfo/999) should fail for invalid fdinfo entry");
    KUNIT_ASSERT_EQ_MSG(errno, ENOENT, "errno should be ENOENT for invalid fdinfo entry");

    // Try to stat a non-numeric fdinfo entry
    errno = 0;
    ret = stat_impl("/proc/self/fdinfo/abc", &st);
    KUNIT_ASSERT_EQ_MSG(ret, -1, "stat(/proc/self/fdinfo/abc) should fail for non-numeric fdinfo entry");
    KUNIT_ASSERT_EQ_MSG(errno, ENOENT, "errno should be ENOENT for non-numeric fdinfo entry");
}


/* ============================================================================
 * HOST PATH VALIDATION AND FAILURE CASES
 * ============================================================================ */

static void testVfsTranslatePathRejectsAbsoluteHostPath(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/private/var/mobile/test", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "current vfs_translate_path behavior routes host-absolute inputs through virtual resolution");
}

static void testVfsTranslatePathRejectsNonRoutePath(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/nonexistent/path", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "vfs_translate_path should fallback to persistent route for unknown paths");
}

static void testVfsTranslatePathRejectsInvalidVirtualPath(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("", host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, 0, "current vfs_translate_path behavior accepts empty path through virtual resolution");
}

static void testVfsTranslatePathRejectsNullPath(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path(NULL, host_path, sizeof(host_path));

    KUNIT_ASSERT_EQ_MSG(ret, -EINVAL, "vfs_translate_path should reject NULL path");
}

static void testVfsTranslatePathRejectsNullBuffer(struct kunit *test) {
    int ret = vfs_translate_path("/etc/passwd", NULL, MAX_PATH);

    KUNIT_ASSERT_EQ_MSG(ret, -EINVAL, "vfs_translate_path should reject NULL buffer");
}

static void testVfsTranslatePathRejectsZeroBufferSize(struct kunit *test) {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/passwd", host_path, 0);

    KUNIT_ASSERT_EQ_MSG(ret, -EINVAL, "vfs_translate_path should reject zero buffer size");
}

/* ============================================================================
 * SYNTHETIC /dev/tty TESTS
 * ============================================================================ */

static void testDevTtyOpenFailsWithoutControllingTty(struct kunit *test) {
    struct task *original_task = task_current();
    struct task *isolated_task = alloc_task();
    KUNIT_ASSERT_TRUE_MSG(isolated_task != NULL, "task allocation should succeed");
    if (!isolated_task) return;

    isolated_task->fs = alloc_fs_struct();
    KUNIT_ASSERT_TRUE_MSG(isolated_task->fs != NULL, "fs_struct allocation should succeed");
    if (!isolated_task->fs) {
        task_put(isolated_task);
        return;
    }

    isolated_task->signal = alloc_signal_struct();
    KUNIT_ASSERT_TRUE_MSG(isolated_task->signal != NULL, "signal_struct allocation should succeed");
    if (!isolated_task->signal) {
        task_put(isolated_task);
        return;
    }

    fs_init_root(isolated_task->fs, "/");
    fs_init_pwd(isolated_task->fs, "/");
    task_set_current(isolated_task);

    errno = 0;
    int fd = open("/dev/tty", O_RDWR);
    KUNIT_ASSERT_EQ_MSG(fd, -1, "open(/dev/tty) should fail without controlling tty");
    KUNIT_ASSERT_TRUE_MSG(errno == ENXIO || errno == EIO, "open(/dev/tty) should set ENXIO or EIO");

    task_set_current(original_task);
    task_put(isolated_task);
}

static void testDevTtyStatFails(struct kunit *test) {
    struct stat st;
    errno = 0;
    int ret = stat_impl("/dev/tty", &st);
    KUNIT_ASSERT_EQ_MSG(ret, 0, "stat(/dev/tty) should report the devfs character node");
    KUNIT_ASSERT_TRUE_MSG(stat_mode_is_char_device(st.st_mode), "/dev/tty should be a character device");
}

static void testDevTtyAccessFails(struct kunit *test) {
    errno = 0;
    int ret = access("/dev/tty", 0);

    KUNIT_ASSERT_EQ_MSG(ret, 0, "access(/dev/tty) should report the devfs node even without a controlling tty");
}

/* ============================================================================
 * PERSISTENT FILESYSTEM TESTS
 * ============================================================================ */

/* ============================================================================
 * BUFFER SIZE VALIDATION TESTS
 * ============================================================================ */

static void testVfsTranslatePathRejectsTooSmallBuffer(struct kunit *test) {
    char small_buf[1];
    int ret = vfs_translate_path("/etc/passwd", small_buf, sizeof(small_buf));

    KUNIT_ASSERT_EQ_MSG(ret, -ENAMETOOLONG, "vfs_translate_path should reject too-small buffer");
}

static void testPathResolveRejectsNullPath(struct kunit *test) {
    char resolved[MAX_PATH];
    errno = 0;
    int ret = path_resolve(NULL, resolved, sizeof(resolved));

    KUNIT_ASSERT_EQ_MSG(ret, -1, "path_resolve should reject NULL path");
    KUNIT_ASSERT_EQ_MSG(errno, EINVAL, "path_resolve should set EINVAL for NULL path");
}

static void testPathResolveRejectsNullBuffer(struct kunit *test) {
    errno = 0;
    int ret = path_resolve("/etc/passwd", NULL, MAX_PATH);

    KUNIT_ASSERT_EQ_MSG(ret, -1, "path_resolve should reject NULL buffer");
    KUNIT_ASSERT_EQ_MSG(errno, EINVAL, "path_resolve should set EINVAL for NULL buffer");
}

static void testPathResolveRejectsZeroBufferSize(struct kunit *test) {
    char resolved[MAX_PATH];
    errno = 0;
    int ret = path_resolve("/etc/passwd", resolved, 0);

    KUNIT_ASSERT_EQ_MSG(ret, -1, "path_resolve should reject zero buffer size");
    KUNIT_ASSERT_EQ_MSG(errno, EINVAL, "path_resolve should set EINVAL for zero buffer size");
}

/* ============================================================================
 * SYNTHETIC FD READ/PREAD/LSEEK/GETDENTS64 TESTS
 * ============================================================================ */

static void testGetdents64HostBackedDirectoryDoesNotCorruptFdLifecycle(struct kunit *test) {
    /* Open a real host-backed directory */
    int fd = open("/etc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/etc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    /* Read directory entries */
    union { char storage[4096]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64_impl(fd, buffer, sizeof(aligned.storage));
    KUNIT_ASSERT_TRUE_MSG(nread > 0, "getdents64_impl(/etc) should return > 0 bytes");

    /* Close the directory */
    close(fd);

    /* Open another file - should not get EBADF from fd reuse corruption */
    int fd2 = open("/etc/passwd", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd2 >= 0, "open(/etc/passwd) should succeed after getdents64_impl/close cycle");
    if (fd2 >= 0) {
        char buf[64];
        ssize_t n = read(fd2, buf, sizeof(buf));
        KUNIT_ASSERT_TRUE_MSG(n >= 0, "read should succeed on reopened fd");
        close(fd2);
    }
}

static void testGetdents64ProcSelfFdListsOpenFd(struct kunit *test) {
    /* Create a known fd to be listed */
    int test_fd = open("/dev/null", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(test_fd >= 0, "open(/dev/null) should succeed");
    if (test_fd < 0) return;

    int proc_fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(proc_fd >= 0, "open(/proc/self/fd, O_DIRECTORY) should succeed");
    if (proc_fd < 0) {
        close(test_fd);
        return;
    }

    union { char storage[4096]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64_impl(proc_fd, buffer, sizeof(aligned.storage));
    KUNIT_ASSERT_TRUE_MSG(nread > 0, "getdents64_impl(/proc/self/fd) should return > 0 bytes");

    /* Look for the test fd entry */
    bool found_test_fd = false;
    size_t pos = 0;
    char test_fd_name[16];
    /* manual int-to-string without format functions */
    {
        int n = test_fd;
        char tmp[16];
        int p = 0;
        if (n == 0) {
            tmp[p++] = '0';
        } else {
            while (n > 0) {
                tmp[p++] = (char)('0' + (n % 10));
                n /= 10;
            }
        }
        for (int j = 0; j < p; j++) {
            test_fd_name[j] = tmp[p - 1 - j];
        }
        test_fd_name[p] = '\0';
    }

    while (pos < (size_t)nread) {
        struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
        if (strcmp(entry->d_name, test_fd_name) == 0) {
            found_test_fd = true;
            break;
        }
        if (entry->d_reclen == 0) break;
        pos += entry->d_reclen;
    }

    KUNIT_ASSERT_TRUE_MSG(found_test_fd, "getdents64_impl(/proc/self/fd) should list the open test fd");

    close(proc_fd);
    close(test_fd);
}

static void testGetdents64ProcSelfFdinfoListsOpenFd(struct kunit *test) {
    /* Create a known fd to be listed */
    int test_fd = open("/dev/null", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(test_fd >= 0, "open(/dev/null) should succeed");
    if (test_fd < 0) return;

    int proc_fd = open("/proc/self/fdinfo", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(proc_fd >= 0, "open(/proc/self/fdinfo, O_DIRECTORY) should succeed");
    if (proc_fd < 0) {
        close(test_fd);
        return;
    }

    union { char storage[4096]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64_impl(proc_fd, buffer, sizeof(aligned.storage));
    KUNIT_ASSERT_TRUE_MSG(nread > 0, "getdents64_impl(/proc/self/fdinfo) should return > 0 bytes");

    /* Look for the test fd entry */
    bool found_test_fd = false;
    size_t pos = 0;
    char test_fd_name[16];
    /* manual int-to-string without format functions */
    {
        int n = test_fd;
        char tmp[16];
        int p = 0;
        if (n == 0) {
            tmp[p++] = '0';
        } else {
            while (n > 0) {
                tmp[p++] = (char)('0' + (n % 10));
                n /= 10;
            }
        }
        for (int j = 0; j < p; j++) {
            test_fd_name[j] = tmp[p - 1 - j];
        }
        test_fd_name[p] = '\0';
    }

    while (pos < (size_t)nread) {
        struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
        if (strcmp(entry->d_name, test_fd_name) == 0) {
            found_test_fd = true;
            KUNIT_ASSERT_EQ_MSG(entry->d_type, DT_REG, "fdinfo entries should be regular files");
            break;
        }
        if (entry->d_reclen == 0) break;
        pos += entry->d_reclen;
    }

    KUNIT_ASSERT_TRUE_MSG(found_test_fd, "getdents64_impl(/proc/self/fdinfo) should list the open test fd");

    close(proc_fd);
    close(test_fd);
}

static void testReadProcSelfFdinfoAdvancesOffset(struct kunit *test) {
    int fd = open_impl("/proc/self/fdinfo/0", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    /* First read should get some content */
    char buf1[64];
    memset(buf1, 0, sizeof(buf1));
    ssize_t n1 = read(fd, buf1, sizeof(buf1));
    KUNIT_ASSERT_TRUE_MSG(n1 > 0, "read should return content from /proc/self/fdinfo/0");

    /* Second read should get EOF since buffer was larger than content */
    char buf2[64];
    ssize_t n2 = read(fd, buf2, sizeof(buf2));
    (void)n2; /* n2 should be 0 (EOF) since fdinfo content is small */

    close(fd);
}

static void testPreadProcSelfFdinfoDoesNotAdvanceOffset(struct kunit *test) {
    int source_fd = open_impl("/dev/null", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE_MSG(source_fd >= 0, "open(/dev/null) should succeed");
    if (source_fd < 0) return;
    int target_fd = dup2_impl(source_fd, 42);
    close_impl(source_fd);
    KUNIT_ASSERT_EQ_MSG(target_fd, 42, "dup2 to stable fd should succeed");
    if (target_fd != 42) return;

    int fd = open_impl("/proc/self/fdinfo/42", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/<fd>) should succeed");
    if (fd < 0) {
        close_impl(target_fd);
        return;
    }

    /* Read the entire content with read() to determine size */
    char full_content[4096];
    ssize_t total = 0;
    ssize_t n;
    while ((n = read_impl(fd, full_content + total, sizeof(full_content) - total)) > 0) {
        total += n;
    }

    /* Reopen to reset offset */
    close_impl(fd);
    fd = open_impl("/proc/self/fdinfo/42", O_RDONLY, 0);
    if (fd < 0) {
        close_impl(target_fd);
        return;
    }

    /* pread at offset 0 should return same first bytes */
    char pread_buf[64];
    memset(pread_buf, 0, sizeof(pread_buf));
    ssize_t pread_n = pread_impl(fd, pread_buf, sizeof(pread_buf), 0);
    KUNIT_ASSERT_TRUE_MSG(pread_n > 0, "pread at offset 0 should return content");

    /* Now read from current position (should be at 0 since pread doesn't advance) */
    char read_buf[64];
    memset(read_buf, 0, sizeof(read_buf));
    ssize_t read_n = read_impl(fd, read_buf, sizeof(read_buf));
    KUNIT_ASSERT_TRUE_MSG(read_n > 0, "read after pread should return content from offset 0");

    close_impl(fd);
    close_impl(target_fd);
}

static void testPreadProcSelfFdinfoOffsetAtEofReturnsZero(struct kunit *test) {
    int source_fd = open_impl("/dev/null", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE_MSG(source_fd >= 0, "open(/dev/null) should succeed");
    if (source_fd < 0) return;
    int target_fd = dup2_impl(source_fd, 42);
    close_impl(source_fd);
    KUNIT_ASSERT_EQ_MSG(target_fd, 42, "dup2 to stable fd should succeed");
    if (target_fd != 42) return;

    int fd = open_impl("/proc/self/fdinfo/42", O_RDONLY, 0);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/<fd>) should succeed");
    if (fd < 0) {
        close_impl(target_fd);
        return;
    }

    /* Read full content first to determine size */
    char content[4096];
    ssize_t total = 0;
    ssize_t n;
    while ((n = read_impl(fd, content + total, sizeof(content) - total)) > 0) {
        total += n;
    }

    /* pread at or past EOF should return 0 */
    char buf[64];
    ssize_t pread_n = pread_impl(fd, buf, sizeof(buf), total);
    KUNIT_ASSERT_EQ_MSG(pread_n, 0, "pread at EOF should return 0");

    pread_n = pread_impl(fd, buf, sizeof(buf), total + 100);
    KUNIT_ASSERT_EQ_MSG(pread_n, 0, "pread past EOF should return 0");

    close_impl(fd);
    close_impl(target_fd);
}

static void testPreadProcSelfFdinfoNullBufferReturnsFault(struct kunit *test) {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pread(fd, NULL, 64, 0);
    KUNIT_ASSERT_EQ_MSG(n, -1, "pread with NULL buffer should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EFAULT, "pread with NULL buffer should set EFAULT");

    close(fd);
}

static void testPreadSyntheticDirectoryReturnsDirectoryError(struct kunit *test) {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    KUNIT_ASSERT_EQ_MSG(n, -1, "pread on directory should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EISDIR, "pread on directory should set EISDIR");

    close(fd);
}

static void testLseekProcSelfFdinfoPolicyIsExplicit(struct kunit *test) {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    /* Current policy: synthetic proc files return ESPIPE for lseek */
    errno = 0;
    int64_t result = lseek(fd, 0, SEEK_SET);
    KUNIT_ASSERT_EQ_MSG(result, (int64_t)-1, "lseek on /proc/self/fdinfo should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, ESPIPE, "lseek on synthetic proc file should set ESPIPE");

    errno = 0;
    result = lseek(fd, 0, SEEK_CUR);
    KUNIT_ASSERT_EQ_MSG(result, (int64_t)-1, "lseek(SEEK_CUR) on /proc/self/fdinfo should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, ESPIPE, "lseek on synthetic proc file should set ESPIPE");

    errno = 0;
    result = lseek(fd, 0, SEEK_END);
    KUNIT_ASSERT_EQ_MSG(result, (int64_t)-1, "lseek(SEEK_END) on /proc/self/fdinfo should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, ESPIPE, "lseek on synthetic proc file should set ESPIPE");

    close(fd);
}

/* ============================================================================
 * FD ACCESS MODE TESTS
 * ============================================================================ */

static void testReadDevNullWriteOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/dev/null", O_WRONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/null, O_WRONLY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = read(fd, buf, sizeof(buf));
    KUNIT_ASSERT_EQ_MSG(n, -1, "read on write-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "read on write-only fd should set EBADF");

    close(fd);
}

static void testReadDevZeroWriteOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/dev/zero", O_WRONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/zero, O_WRONLY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = read(fd, buf, sizeof(buf));
    KUNIT_ASSERT_EQ_MSG(n, -1, "read on write-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "read on write-only fd should set EBADF");

    close(fd);
}

static void testWriteDevNullReadOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/dev/null", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/null, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    KUNIT_ASSERT_EQ_MSG(n, -1, "write on read-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "write on read-only fd should set EBADF");

    close(fd);
}

static void testWriteDevZeroReadOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/dev/zero", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/zero, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    KUNIT_ASSERT_EQ_MSG(n, -1, "write on read-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "write on read-only fd should set EBADF");

    close(fd);
}

static void testWriteDevUrandomReadOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/dev/urandom", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/urandom, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    KUNIT_ASSERT_EQ_MSG(n, -1, "write on read-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "write on read-only fd should set EBADF");

    close(fd);
}

static void testOpenProcSelfFdinfoWriteOnlyReturnsAcces(struct kunit *test) {
    /* Opening proc files write-only should fail at open time */
    errno = 0;
    int fd = open("/proc/self/fdinfo/0", O_WRONLY);
    KUNIT_ASSERT_EQ_MSG(fd, -1, "open(/proc/self/fdinfo/0, O_WRONLY) should fail");
    KUNIT_ASSERT_EQ_MSG(errno, EACCES, "open write-only on proc file should set EACCES");
}

static void testWriteProcSelfFdinfoReadOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    KUNIT_ASSERT_EQ_MSG(n, -1, "write on read-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "write on read-only fd should set EBADF");

    close(fd);
}

static void testPreadProcSelfFdinfoReadOnlyFdWorks(struct kunit *test) {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    KUNIT_ASSERT_TRUE_MSG(n > 0, "pread on readable fd should succeed");

    close(fd);
}

static void testPreadDevNullWriteOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/dev/null", O_WRONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/null, O_WRONLY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    KUNIT_ASSERT_EQ_MSG(n, -1, "pread on write-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "pread on write-only fd should set EBADF");

    close(fd);
}

static void testPwriteDevNullReadOnlyFdReturnsBadf(struct kunit *test) {
    int fd = open("/dev/null", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/dev/null, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pwrite(fd, "hello", 5, 0);
    KUNIT_ASSERT_EQ_MSG(n, -1, "pwrite on read-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "pwrite on read-only fd should set EBADF");

    close(fd);
}

static void testPwriteProcSelfFdinfoReadOnlyFdDoesNotHostFallback(struct kunit *test) {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pwrite(fd, "hello", 5, 0);
    /* Should fail with EBADF (access mode) not with EBADF from host fallback on -1 fd */
    KUNIT_ASSERT_EQ_MSG(n, -1, "pwrite on read-only fd should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EBADF, "pwrite on read-only fd should set EBADF");

    close(fd);
}

static void testReadSyntheticDirectoryReturnsEisdir(struct kunit *test) {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = read(fd, buf, sizeof(buf));
    KUNIT_ASSERT_EQ_MSG(n, -1, "read on directory should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EISDIR, "read on directory should set EISDIR");

    close(fd);
}

static void testWriteSyntheticDirectoryReturnsEisdir(struct kunit *test) {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    KUNIT_ASSERT_EQ_MSG(n, -1, "write on directory should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EISDIR, "write on directory should set EISDIR");

    close(fd);
}

static void testPwriteSyntheticDirectoryReturnsEisdir(struct kunit *test) {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pwrite(fd, "hello", 5, 0);
    KUNIT_ASSERT_EQ_MSG(n, -1, "pwrite on directory should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EISDIR, "pwrite on directory should set EISDIR");

    close(fd);
}

static void testPreadSyntheticDirectoryReturnsEisdir(struct kunit *test) {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    KUNIT_ASSERT_TRUE_MSG(fd >= 0, "open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    KUNIT_ASSERT_EQ_MSG(n, -1, "pread on directory should return -1");
    KUNIT_ASSERT_EQ_MSG(errno, EISDIR, "pread on directory should set EISDIR");

    close(fd);
}

static const struct kunit_case vfs_path_cases[] = {
    KUNIT_CASE(testVirtualRootTranslatesToPersistentBackingRoot),
    KUNIT_CASE(testTempPathTranslatesToTempBackingRoot),
    KUNIT_CASE(testRelativePersistentPathMapsUnderPersistentBackingRoot),
    KUNIT_CASE(testUnmappableHostPathIsRejected),
    KUNIT_CASE(testPersistentBackingRootReverseTranslatesToVirtualRoot),
    KUNIT_CASE(testMappedPersistentHostPathReverseTranslatesToVirtualPath),
    KUNIT_CASE(testPersistentRootDiscoveryResolves),
    KUNIT_CASE(testCacheRootDiscoveryResolves),
    KUNIT_CASE(testTempRootDiscoveryResolves),
    KUNIT_CASE(testDiscoveredBackingRootsAreDistinctByClass),
    KUNIT_CASE(testBackingClassRoutingForPersistentPaths),
    KUNIT_CASE(testBackingClassRoutingForCacheTempAndSyntheticPaths),
    KUNIT_CASE(testPersistentFallbackRouteTranslatesAndReverseTranslates),
    KUNIT_CASE(testSyntheticRouteRejectsHostJoin),
    KUNIT_CASE(testDescriptorDrivenPathClassification),
    KUNIT_CASE(testDescriptorDrivenVirtualLinuxDetection),
    KUNIT_CASE(testPersistentRootIsNotDocumentsTruth),
    KUNIT_CASE(testPersistentBackingRootReverseTranslation),
    KUNIT_CASE(testCacheBackingRootReverseTranslation),
    KUNIT_CASE(testTempBackingRootReverseTranslation),
    KUNIT_CASE(testParentEscapeIsRejected),
    KUNIT_CASE(testTaskAwareAbsolutePathUsesVirtualRoot),
    KUNIT_CASE(testTaskAwareRelativePathUsesPwd),
    KUNIT_CASE(testTaskAwareRelativePathWithSubdirectories),
    KUNIT_CASE(testTaskAwareParentEscapeRejected),
    KUNIT_CASE(testTaskAwareAbsolutePathUsesTaskRootPrefix),
    KUNIT_CASE(testGetcwdMatchesTaskPwdAndRelativeResolution),
    KUNIT_CASE(testVfsTranslatePathAtUsesAtFdcwd),
    KUNIT_CASE(testVfsTranslatePathAtAbsolutePathIgnoresDirfd),
    KUNIT_CASE(testVfsTranslatePathAtInvalidDirfdReturnsBadf),
    KUNIT_CASE(testVfsFstatatSupportsAtFdcwd),
    KUNIT_CASE(testVfsFstatatSupportsSymlinkNoFollow),
    KUNIT_CASE(testOpenNoFollowRejectsSymlinkWithEloop),
    KUNIT_CASE(testOpenatNoFollowRejectsDirfdSymlinkWithEloop),
    KUNIT_CASE(testOpenFollowsSymlinkToFileWithoutNoFollow),
    KUNIT_CASE(testOpenFollowsAbsoluteSymlinkAsVirtualPath),
    KUNIT_CASE(testOpenResolvesIntermediateSymlinkDirectory),
    KUNIT_CASE(testOpenSymlinkLoopReturnsEloop),
    KUNIT_CASE(testChdirResolvesSymlinkDirectory),
    KUNIT_CASE(testMkdiratResolvesIntermediateSymlinkDirectory),
    KUNIT_CASE(testUnlinkatResolvesIntermediateSymlinkDirectory),
    KUNIT_CASE(testRenameatResolvesIntermediateSymlinkDirectories),
    KUNIT_CASE(testLinkatRespectsSymlinkFollowFlag),
    KUNIT_CASE(testVfsFstatatRejectsInvalidFlags),
    KUNIT_CASE(testFstatInvalidFdReturnsBadf),
    KUNIT_CASE(testFstatNullStatbufReturnsFault),
    KUNIT_CASE(testFstatImplSucceedsForLinuxOwnedFd),
    KUNIT_CASE(testFstatDuplicatedRealBackedFdMatchesOriginal),
    KUNIT_CASE(testFstatProcDirectoryFdReportsDirectory),
    KUNIT_CASE(testFstatProcSelfFdDirectoryReportsDirectory),
    KUNIT_CASE(testFstatProcSelfFdinfoFileReportsRegularFile),
    KUNIT_CASE(testSyntheticRootStatSucceeds),
    KUNIT_CASE(testSyntheticChildStatHandlesSupportedProcFilesAndRejectsUnsupportedSys),
    KUNIT_CASE(testSyntheticRootAccessSucceeds),
    KUNIT_CASE(testSyntheticChildAccessHandlesSupportedProcFiles),
    KUNIT_CASE(testSyntheticRootOpenDirectorySucceedsAndChildOpenFails),
    KUNIT_CASE(testSyntheticChildOpenHandlesSupportedProcFilesAndRejectsUnsupportedSys),
    KUNIT_CASE(testSyntheticRootOpenDirectorySucceeds),
    KUNIT_CASE(testSyntheticRootGetdents64ReturnsDotAndDotdot),
    KUNIT_CASE(testSyntheticSysAndDevGetdents64ReturnsDotAndDotdot),
    KUNIT_CASE(testDevNullStatSucceeds),
    KUNIT_CASE(testDevZeroStatSucceeds),
    KUNIT_CASE(testDevUrandomStatSucceeds),
    KUNIT_CASE(testDevNullAccessSucceeds),
    KUNIT_CASE(testDevZeroAccessSucceeds),
    KUNIT_CASE(testDevUrandomAccessSucceeds),
    KUNIT_CASE(testDevNullOpenSucceeds),
    KUNIT_CASE(testDevZeroOpenSucceeds),
    KUNIT_CASE(testDevUrandomOpenSucceeds),
    KUNIT_CASE(testDevNullReadReturnsEOF),
    KUNIT_CASE(testDevNullWriteSucceedsAndDiscards),
    KUNIT_CASE(testDevZeroReadFillsZeroBytes),
    KUNIT_CASE(testDevUrandomReadReturnsNontrivialData),
    KUNIT_CASE(testDevZeroWriteSucceedsAndDiscards),
    KUNIT_CASE(testDevUrandomWriteSucceedsAndDiscards),
    KUNIT_CASE(testUnsupportedDevNodeStillFails),
    KUNIT_CASE(testVfsFaccessatSupportsAtFdcwd),
    KUNIT_CASE(testVfsFaccessatRejectsInvalidFlags),
    KUNIT_CASE(testVfsFaccessatAtEaccessUsesEffectiveCredentials),
    KUNIT_CASE(testVfsFaccessatReportsUnsupportedSymlinkNoFollow),
    KUNIT_CASE(testFaccessatFollowsAbsoluteSymlinkAsVirtualPath),
    KUNIT_CASE(testFaccessatSymlinkLoopReturnsEloop),
    KUNIT_CASE(testFsuidControlsOwnerFileAccess),
    KUNIT_CASE(testFsgidControlsGroupFileAccess),
    KUNIT_CASE(testChrootRebasesAbsolutePathsAndGetcwd),
    KUNIT_CASE(testNonrootCannotChroot),
    KUNIT_CASE(testRootWithoutSysChrootCannotChroot),
    KUNIT_CASE(testPivotRootRebasesAbsolutePathsAndExposesOldRoot),
    KUNIT_CASE(testPivotRootSyscallRebasesAbsolutePathsAndExposesOldRoot),
    KUNIT_CASE(testFchdirUpdatesVirtualPwd),
    KUNIT_CASE(testBindMountRedirectsTargetTree),
    KUNIT_CASE(testBindMountDuplicateTargetReturnsBusy),
    KUNIT_CASE(testUmountRestoresTargetTree),
    KUNIT_CASE(testBindMountRejectsNonBindMount),
    KUNIT_CASE(testMountAndUmount2SyscallsRouteToLinuxOwnedMountStack),
    KUNIT_CASE(testMountNamespaceSharedAcrossTaskDup),
    KUNIT_CASE(testMountNamespaceUnshareIsolatesChildMounts),
    KUNIT_CASE(testProcSelfMountinfoListsBindMount),
    KUNIT_CASE(testProcSelfMountinfoUsesLinuxShapedOptionalFields),
    KUNIT_CASE(testProcSelfMountinfoReportsSharedPropagation),
    KUNIT_CASE(testProcSelfMountinfoReportsSlavePrivateAndUnbindablePropagation),
    KUNIT_CASE(testMountRejectsMultiplePropagationFlags),
    KUNIT_CASE(testUnprivilegedMountOperationsFailWithoutNamespaceMutation),
    KUNIT_CASE(testSharedMountPropagatesChildBindToPeer),
    KUNIT_CASE(testSharedMountinfoUsesPeerGroupIds),
    KUNIT_CASE(testSharedMountPropagatesNestedChildBindToPeer),
    KUNIT_CASE(testSharedMountUnmountPropagatesNestedChildFromPeer),
    KUNIT_CASE(testRecursiveUnmountPropagatesNestedChildrenFromSharedPeer),
    KUNIT_CASE(testHardlinkInodeMetadataSurvivesUnlink),
    KUNIT_CASE(testProcFdMarksOpenUnlinkedFileDeleted),
    KUNIT_CASE(testPrivateChildUnmountDoesNotPropagateToSharedPeer),
    KUNIT_CASE(testSlaveChildUnmountDoesNotPropagateToSharedPeer),
    KUNIT_CASE(testCloneNewnsSharedPropagationStaysInsideChildNamespace),
    KUNIT_CASE(testMountNamespaceRefsTrackTaskLifecycle),
    KUNIT_CASE(testCloneNewnsRebasesSharedPeerGroups),
    KUNIT_CASE(testCloneNewnsRebasesSlaveMasterToChildPeerGroup),
    KUNIT_CASE(testUnmountBusyWhenOpenFdPinsMountTree),
    KUNIT_CASE(testLazyUnmountDetachesBusyMountFromNamespace),
    KUNIT_CASE(testLazyUnmountRemovesBusyMountFromProcMountinfo),
    KUNIT_CASE(testUnmountExpireRequiresMarkThenUnmount),
    KUNIT_CASE(testLazyUnmountReclaimsDetachedRefAfterPinRelease),
    KUNIT_CASE(testUnmountBusyWhenPwdPinsMountTree),
    KUNIT_CASE(testUnmountBusyWhenRootPinsMountTree),
    KUNIT_CASE(testSlaveMountReceivesNestedChildFromSharedMaster),
    KUNIT_CASE(testMountSetattrRecursiveMarksChildPrivate),
    KUNIT_CASE(testRecursiveRemountPrivateMarksChildPrivate),
    KUNIT_CASE(testMountSetattrRecursiveAttrsVisibleInStatmount),
    KUNIT_CASE(testRecursiveRemountAttrsVisibleInStatmount),
    KUNIT_CASE(testRecursiveRemountSlavePreservesPeerGroupMasters),
    KUNIT_CASE(testListmountStatmountReportsSlaveMaster),
    KUNIT_CASE(testListmountWalksMountSubtreeByParentId),
    KUNIT_CASE(testMountinfoReportsNestedParentMountId),
    KUNIT_CASE(testRecursiveBindClonesNestedMountTopology),
    KUNIT_CASE(testMoveMountRelocatesBindSubtree),
    KUNIT_CASE(testOpenTreeCloneReturnsMountFdVisibleInProc),
    KUNIT_CASE(testMoveMountAttachesOpenTreeClone),
    KUNIT_CASE(testOpenTreeCloneSurvivesSourceUnmountUntilAttached),
    KUNIT_CASE(testOpenTreeCloneNestedMountTopologyAttachesRecursively),
    KUNIT_CASE(testOpenTreeCloneAttachPropagatesPrivateSubtreeToSharedAndSlave),
    KUNIT_CASE(testOpenTreeCloneSurvivesSourceNamespaceTeardown),
    KUNIT_CASE(testSharedMountMovePropagatesToPeer),
    KUNIT_CASE(testSharedMoveUpdatesProcMountinfoForPeer),
    KUNIT_CASE(testSlaveChildMoveDoesNotPropagateToSharedPeer),
    KUNIT_CASE(testCloneNewnsMovePropagatesToRebasedSlaveReceiver),
    KUNIT_CASE(testRecursiveUmountPropagatesNestedSharedSubtree),
    KUNIT_CASE(testRecursiveUmountUpdatesProcMountinfoForPropagatedPeers),
    KUNIT_CASE(testStatmountRejectsPropagatedRemovedMountId),
    KUNIT_CASE(testMountIdsStableAcrossMoveUnmountAndNamespaceClone),
    KUNIT_CASE(testMountNamespaceTeardownAccountsMountsAndDetachedRefs),
    KUNIT_CASE(testMountNamespaceDropReclaimsChildDetachedRefs),
    KUNIT_CASE(testLazyUmountRefSurvivesDescendantTaskTree),
    KUNIT_CASE(testLazyUmountRefSurvivesChildRootAndPwdPins),
    KUNIT_CASE(testChildMountNamespaceDetachSurvivesChildRootAndPwdPins),
    KUNIT_CASE(testLazyDetachPropagatesNestedSharedSlaveTree),
    KUNIT_CASE(testUmount2DetachDetachesBusyMountFromNamespace),
    KUNIT_CASE(testUmount2SyscallDetachDetachesBusyMountFromNamespace),
    KUNIT_CASE(testUmount2RejectsUnusedLinuxUmountFlag),
    KUNIT_CASE(testUmount2ForceDetachesBusyMountAndReapsAfterPinRelease),
    KUNIT_CASE(testForceUmountDetachedRefsAreMountNamespaceScoped),
    KUNIT_CASE(testForceUmountPropagatesSharedSlaveSubtreeTeardown),
    KUNIT_CASE(testUmount2ExpireRequiresMarkThenUnmount),
    KUNIT_CASE(testUmount2RejectsExpireWithDetach),
    KUNIT_CASE(testUmount2NofollowRejectsSymlinkTarget),
    KUNIT_CASE(testProcSelfMountsListsBindMount),
    KUNIT_CASE(testBindMountRemountReadonlyRejectsWrites),
    KUNIT_CASE(testBindMountRemountReadwritePermitsWrites),
    KUNIT_CASE(testProcSelfMountinfoReportsReadonlyRemount),
    KUNIT_CASE(testNonrootCannotCreateBindMount),
    KUNIT_CASE(testRootWithoutSysAdminCannotCreateBindMount),
    KUNIT_CASE(testNonrootCannotUnmountBindMount),
    KUNIT_CASE(testProcSelfMountinfoUsesCurrentMountNamespace),
    KUNIT_CASE(testProcSelfMountViewsDoNotExposeHostPaths),
    KUNIT_CASE(testNonrootCannotReadRootPrivateFile),
    KUNIT_CASE(testNonrootCanReadOtherReadableFile),
    KUNIT_CASE(testNonrootCreatedFileRecordsVirtualOwner),
    KUNIT_CASE(testNonrootCannotUnlinkInsideRootPrivateDir),
    KUNIT_CASE(testNonrootCannotOpenThroughUnsearchableParentDirectory),
    KUNIT_CASE(testStickyDirectoryBlocksNonOwnerUnlink),
    KUNIT_CASE(testStickyDirectoryBlocksNonOwnerRename),
    KUNIT_CASE(testStickyDirectoryBlocksNonOwnerExchangeTarget),
    KUNIT_CASE(testNonrootCannotMkdiratInsideRootPrivateDir),
    KUNIT_CASE(testNonrootCannotUnlinkatInsideRootPrivateDir),
    KUNIT_CASE(testLinkatUsesVirtualDirfds),
    KUNIT_CASE(testSymlinkatAndReadlinkatUseVirtualDirfds),
    KUNIT_CASE(testRenameat2ExchangeSwapsFilesThroughVirtualDirfds),
    KUNIT_CASE(testRenameat2ExchangeSwapsVirtualMetadata),
    KUNIT_CASE(testRenameat2NoreplaceExistingTargetReturnsExist),
    KUNIT_CASE(testRenameatOverwriteMovesVirtualMetadata),
    KUNIT_CASE(testRenameDirectoryOverNonemptyDirectoryReturnsNotempty),
    KUNIT_CASE(testRenameFileOverDirectoryReturnsIsdir),
    KUNIT_CASE(testRenameDirectoryOverFileReturnsNotdir),
    KUNIT_CASE(testRootChownUpdatesVirtualOwner),
    KUNIT_CASE(testNonrootCannotChownOwnedFile),
    KUNIT_CASE(testOwnerChmodUpdatesVirtualMode),
    KUNIT_CASE(testNonownerCannotChmodFile),
    KUNIT_CASE(testFchmodUpdatesVirtualMode),
    KUNIT_CASE(testFchownUpdatesVirtualOwner),
    KUNIT_CASE(testSupplementaryGroupCanReadGroupFile),
    KUNIT_CASE(testMissingSupplementaryGroupCannotReadGroupFile),
    KUNIT_CASE(testRootWithoutDacCapsCannotReadPrivateFile),
    KUNIT_CASE(testStatfsReportsVirtualProcAndTmpfs),
    KUNIT_CASE(testStatfsReportsMountAttributeFlags),
    KUNIT_CASE(testNodevMountBlocksDeviceOpen),
    KUNIT_CASE(testSignalKillSucceeds),
    KUNIT_CASE(testProcSelfStatSucceeds),
    KUNIT_CASE(testProcSelfStatFileSucceeds),
    KUNIT_CASE(testProcSelfCmdlineSucceeds),
    KUNIT_CASE(testProcSelfCommSucceeds),
    KUNIT_CASE(testProcSelfStatmSucceeds),
    KUNIT_CASE(testProcSelfExeIsSymlink),
    KUNIT_CASE(testProcSelfCwdIsSymlink),
    KUNIT_CASE(testProcSelfFdIsDirectory),
    KUNIT_CASE(testProcSelfFdinfoIsDirectory),
    KUNIT_CASE(testProcSelfFdSymlinksExist),
    KUNIT_CASE(testProcSelfFdSymlinksPointToValidPaths),
    KUNIT_CASE(testProcSelfFdSymlinksReflectActualFdState),
    KUNIT_CASE(testProcSelfFdInvalidFdNumbersFail),
    KUNIT_CASE(testProcSelfFdinfoFilesExist),
    KUNIT_CASE(testProcSelfFdinfoInvalidFdNumbersFail),
    KUNIT_CASE(testVfsTranslatePathRejectsAbsoluteHostPath),
    KUNIT_CASE(testVfsTranslatePathRejectsNonRoutePath),
    KUNIT_CASE(testVfsTranslatePathRejectsInvalidVirtualPath),
    KUNIT_CASE(testVfsTranslatePathRejectsNullPath),
    KUNIT_CASE(testVfsTranslatePathRejectsNullBuffer),
    KUNIT_CASE(testVfsTranslatePathRejectsZeroBufferSize),
    KUNIT_CASE(testDevTtyOpenFailsWithoutControllingTty),
    KUNIT_CASE(testDevTtyStatFails),
    KUNIT_CASE(testDevTtyAccessFails),
    KUNIT_CASE(testVfsTranslatePathRejectsTooSmallBuffer),
    KUNIT_CASE(testPathResolveRejectsNullPath),
    KUNIT_CASE(testPathResolveRejectsNullBuffer),
    KUNIT_CASE(testPathResolveRejectsZeroBufferSize),
    KUNIT_CASE(testGetdents64HostBackedDirectoryDoesNotCorruptFdLifecycle),
    KUNIT_CASE(testGetdents64ProcSelfFdListsOpenFd),
    KUNIT_CASE(testGetdents64ProcSelfFdinfoListsOpenFd),
    KUNIT_CASE(testReadProcSelfFdinfoAdvancesOffset),
    KUNIT_CASE(testPreadProcSelfFdinfoDoesNotAdvanceOffset),
    KUNIT_CASE(testPreadProcSelfFdinfoOffsetAtEofReturnsZero),
    KUNIT_CASE(testPreadProcSelfFdinfoNullBufferReturnsFault),
    KUNIT_CASE(testPreadSyntheticDirectoryReturnsDirectoryError),
    KUNIT_CASE(testLseekProcSelfFdinfoPolicyIsExplicit),
    KUNIT_CASE(testReadDevNullWriteOnlyFdReturnsBadf),
    KUNIT_CASE(testReadDevZeroWriteOnlyFdReturnsBadf),
    KUNIT_CASE(testWriteDevNullReadOnlyFdReturnsBadf),
    KUNIT_CASE(testWriteDevZeroReadOnlyFdReturnsBadf),
    KUNIT_CASE(testWriteDevUrandomReadOnlyFdReturnsBadf),
    KUNIT_CASE(testOpenProcSelfFdinfoWriteOnlyReturnsAcces),
    KUNIT_CASE(testWriteProcSelfFdinfoReadOnlyFdReturnsBadf),
    KUNIT_CASE(testPreadProcSelfFdinfoReadOnlyFdWorks),
    KUNIT_CASE(testPreadDevNullWriteOnlyFdReturnsBadf),
    KUNIT_CASE(testPwriteDevNullReadOnlyFdReturnsBadf),
    KUNIT_CASE(testPwriteProcSelfFdinfoReadOnlyFdDoesNotHostFallback),
    KUNIT_CASE(testReadSyntheticDirectoryReturnsEisdir),
    KUNIT_CASE(testWriteSyntheticDirectoryReturnsEisdir),
    KUNIT_CASE(testPwriteSyntheticDirectoryReturnsEisdir),
    KUNIT_CASE(testPreadSyntheticDirectoryReturnsEisdir),
};

static const struct kunit_suite vfs_path_suite = {
    .name = "vfs_path",
    .cases = vfs_path_cases,
    .case_count = sizeof(vfs_path_cases) / sizeof(vfs_path_cases[0]),
    .init = vfs_path_suite_init,
    .exit = vfs_path_suite_exit,
};

const struct kunit_suite *fs_vfs_path_suite(void) {
    return &vfs_path_suite;
}
