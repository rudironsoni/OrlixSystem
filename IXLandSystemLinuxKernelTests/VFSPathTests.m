/*
 * IXLandSystemTests - VFSPathTests.m (Linux-only)
 *
 * INTERNAL OWNER SEMANTIC TESTS ONLY.
 * Host-dependent helpers and NSFileManager usage are removed and live in the
 * HostBridge test target to preserve strict separation.
 */

#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

/* Minimal standard headers */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

/* Standard system headers */
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

/* IXLand VFS types */
#include "fs/vfs.h"
#include "fs/path.h"
#include "kernel/task.h"
#include "kernel/signal.h"
#include "kernel/init.h"
#include "kernel/cred_internal.h"
#include "runtime/native/registry.h"

/* Linux UAPI test support - semantic helpers only */
#include "IXLandSystemLinuxKernelTests/LinuxUAPITestSupport.h"

#ifndef INVALID_FLAG_TEST_VALUE
#define INVALID_FLAG_TEST_VALUE 0x40000000u
#endif

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

extern ssize_t getdents64(int fd, void *dirp, size_t count);
extern char *getcwd_impl(char *buf, size_t size);
extern int vfs_discover_persistent_root(char *path, size_t size);
extern int vfs_discover_cache_root(char *path, size_t size);
extern int vfs_discover_temp_root(char *path, size_t size);
extern int stat_impl(const char *path, struct linux_stat *statbuf);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern int lstat_impl(const char *path, struct linux_stat *statbuf);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int dup2_impl(int oldfd, int newfd);
extern long read_impl(int fd, void *buf, size_t count);
extern long pread_impl(int fd, void *buf, size_t count, linux_off_t offset);
extern void cred_reset_to_defaults(void);
extern int vfs_path_contract_open_tmp_fd_symlink_file(void);

@interface VFSPathTests : XCTestCase
@end

@implementation VFSPathTests

- (void)setUp {
    [super setUp];
    /* Ensure kernel is booted for VFS operations that use current task */
    int ret = start_kernel();
    XCTAssertEqual(ret, 0, @"start_kernel must succeed before LinuxKernel VFS tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before LinuxKernel VFS tests");
    cred_reset_to_defaults();
    /* Clean up any lingering file descriptors using owner close_impl */
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

- (void)tearDown {
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
    cred_reset_to_defaults();
    [super tearDown];
}

/*
 * Keep only owner-focused tests here. Host-dependent file seeding and
 * NSFileManager-based operations were moved to the HostBridge test target.
 */

/* ============================================================================
 * PATH TRANSLATION TESTS
 * ============================================================================ */

- (void)testVirtualRootTranslatesToPersistentBackingRoot {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path should accept virtual root");
    XCTAssertEqual(strcmp(host_path, vfs_persistent_backing_root()), 0,
                   @"virtual root should map to persistent backing root");
}

- (void)testTempPathTranslatesToTempBackingRoot {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/tmp/demo", host_path, sizeof(host_path));
    NSString *actualPath = [NSString stringWithUTF8String:host_path];
    NSString *expectedPath = [NSString stringWithFormat:@"%s/demo", vfs_temp_backing_root()];

    XCTAssertEqual(ret, 0, @"temp virtual path should translate");
    XCTAssertEqualObjects(actualPath, expectedPath, @"temp path should resolve under temp backing root");
    XCTAssertNotEqual(strcmp(host_path, "/tmp/demo"), 0,
                      @"translation must not be identity mapping");
}

- (void)testRelativePersistentPathMapsUnderPersistentBackingRoot {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("etc/passwd", host_path, sizeof(host_path));
    NSString *actualPath = [NSString stringWithUTF8String:host_path];
    NSString *expectedPath = [NSString stringWithFormat:@"%s/etc/passwd", vfs_persistent_backing_root()];

    XCTAssertEqual(ret, 0, @"relative virtual path should translate");
    XCTAssertEqualObjects(actualPath, expectedPath, @"relative path should resolve under persistent backing root");
}

- (void)testUnmappableHostPathIsRejected {
    char virtual_path[MAX_PATH];
    int ret = vfs_reverse_translate("/private/var/mobile", virtual_path, sizeof(virtual_path));

    XCTAssertEqual(ret, -EXDEV, @"unmapped host path should be rejected");
}

- (void)testPersistentBackingRootReverseTranslatesToVirtualRoot {
    char virtual_path[MAX_PATH];
    int ret = vfs_reverse_translate(vfs_persistent_backing_root(), virtual_path, sizeof(virtual_path));

    XCTAssertEqual(ret, 0, @"persistent backing root should reverse translate");
    XCTAssertEqual(strcmp(virtual_path, vfs_virtual_root()), 0,
                   @"persistent backing root should map back to virtual root");
}

- (void)testMappedPersistentHostPathReverseTranslatesToVirtualPath {
    char virtual_path[MAX_PATH];
    NSString *hostPath = [NSString stringWithFormat:@"%s/var/log", vfs_persistent_backing_root()];
    int ret = vfs_reverse_translate(hostPath.UTF8String, virtual_path, sizeof(virtual_path));

    XCTAssertEqual(ret, 0, @"mapped persistent host path should reverse translate");
    XCTAssertEqualObjects([NSString stringWithUTF8String:virtual_path], @"/var/log",
                          @"reverse translation should return virtual path");
}

- (void)testPersistentRootDiscoveryResolves {
    char path[MAX_PATH];
    int ret = vfs_discover_persistent_root(path, sizeof(path));

    XCTAssertEqual(ret, 0, @"persistent root discovery should succeed");
    XCTAssertTrue(path[0] != '\0', @"persistent root should be non-empty");
}

- (void)testCacheRootDiscoveryResolves {
    char path[MAX_PATH];
    int ret = vfs_discover_cache_root(path, sizeof(path));

    XCTAssertEqual(ret, 0, @"cache root discovery should succeed");
    XCTAssertTrue(path[0] != '\0', @"cache root should be non-empty");
}

- (void)testTempRootDiscoveryResolves {
    char path[MAX_PATH];
    int ret = vfs_discover_temp_root(path, sizeof(path));

    XCTAssertEqual(ret, 0, @"temp root discovery should succeed");
    XCTAssertTrue(path[0] != '\0', @"temp root should be non-empty");
}

- (void)testDiscoveredBackingRootsAreDistinctByClass {
    char persistent[MAX_PATH];
    char cache[MAX_PATH];
    char temp[MAX_PATH];

    XCTAssertEqual(vfs_discover_persistent_root(persistent, sizeof(persistent)), 0,
                   @"persistent root discovery should succeed");
    XCTAssertEqual(vfs_discover_cache_root(cache, sizeof(cache)), 0,
                   @"cache root discovery should succeed");
    XCTAssertEqual(vfs_discover_temp_root(temp, sizeof(temp)), 0,
                   @"temp root discovery should succeed");

    XCTAssertNotEqual(strcmp(persistent, temp), 0,
                      @"persistent root must not be temp-backed");
    XCTAssertNotEqual(strcmp(cache, temp), 0,
                      @"cache and temp roots should not collapse to one path");
}

- (void)testBackingClassRoutingForPersistentPaths {
    XCTAssertEqual(vfs_backing_class_for_path("/etc/passwd"), VFS_BACKING_PERSISTENT,
                   @"/etc/passwd should route to persistent backing");
    XCTAssertEqual(vfs_backing_class_for_path("/usr/bin/sh"), VFS_BACKING_PERSISTENT,
                   @"/usr/bin/sh should route to persistent backing");
    XCTAssertEqual(vfs_backing_class_for_path("/var/lib/foo"), VFS_BACKING_PERSISTENT,
                   @"/var/lib/foo should route to persistent backing");
    XCTAssertEqual(vfs_backing_class_for_path("/home/user/.profile"), VFS_BACKING_PERSISTENT,
                   @"/home paths should route to persistent backing");
    XCTAssertEqual(vfs_backing_class_for_path("/root/.profile"), VFS_BACKING_PERSISTENT,
                   @"/root paths should route to persistent backing");
}

- (void)testBackingClassRoutingForCacheTempAndSyntheticPaths {
    XCTAssertEqual(vfs_backing_class_for_path("/var/cache/x"), VFS_BACKING_CACHE,
                   @"/var/cache/x should route to cache backing");
    XCTAssertEqual(vfs_backing_class_for_path("/tmp/x"), VFS_BACKING_TEMP,
                   @"/tmp/x should route to temp backing");
    XCTAssertEqual(vfs_backing_class_for_path("/proc/meminfo"), VFS_BACKING_SYNTHETIC,
                   @"/proc/meminfo should route to synthetic backing");
    XCTAssertEqual(vfs_backing_class_for_path("/sys/kernel"), VFS_BACKING_SYNTHETIC,
                   @"/sys/kernel should route to synthetic backing");
    XCTAssertEqual(vfs_backing_class_for_path("/dev/null"), VFS_BACKING_SYNTHETIC,
                   @"/dev/null should route to synthetic backing");
}

- (void)testPersistentFallbackRouteTranslatesAndReverseTranslates {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    int ret;
    NSString *actualPath;
    NSString *expectedPath;

    ret = vfs_translate_path("/var/log/messages", host_path, sizeof(host_path));
    XCTAssertEqual(ret, 0, @"fallback persistent path should translate");

    actualPath = [NSString stringWithUTF8String:host_path];
    expectedPath = [NSString stringWithFormat:@"%s/var/log/messages", vfs_persistent_backing_root()];
    XCTAssertEqualObjects(actualPath, expectedPath,
                          @"fallback persistent route should join under persistent backing root");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    XCTAssertEqual(ret, 0, @"fallback persistent host path should reverse translate");
    XCTAssertEqualObjects([NSString stringWithUTF8String:virtual_path], @"/var/log/messages",
                          @"fallback persistent host path should map back through the route table");
}

- (void)testSyntheticRouteRejectsHostJoin {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/dev/null", host_path, sizeof(host_path));

    XCTAssertEqual(ret, -ENOTSUP, @"synthetic route should not join to a host backing root");
}

- (void)testDescriptorDrivenPathClassification {
    XCTAssertEqual(path_classify("/proc/meminfo"), PATH_VIRTUAL_LINUX,
                   @"synthetic routes should classify through descriptor lookup");
    XCTAssertEqual(path_classify("/sys/kernel"), PATH_VIRTUAL_LINUX,
                   @"sys routes should classify through descriptor lookup");
    XCTAssertEqual(path_classify("/dev/null"), PATH_VIRTUAL_LINUX,
                   @"dev routes should classify through descriptor lookup");
    XCTAssertEqual(path_classify("/private/var/mobile/file"), PATH_ABSOLUTE_HOST,
                   @"non-route absolute paths should remain host paths");
}

- (void)testDescriptorDrivenVirtualLinuxDetection {
    XCTAssertTrue(path_is_virtual_linux("/tmp/demo"),
                  @"tmp route should be recognized as Linux-visible");
    XCTAssertTrue(path_is_virtual_linux("/var/tmp/demo"),
                  @"var/tmp route should remain a distinct Linux-visible route");
    XCTAssertTrue(path_is_virtual_linux("/run/demo"),
                  @"run route should remain a distinct Linux-visible route");
    XCTAssertTrue(path_is_virtual_linux("relative/demo"),
                  @"relative paths should stay Linux-visible by context");
    XCTAssertFalse(path_is_virtual_linux("/private/var/mobile/file"),
                   @"absolute host paths outside route descriptors should not classify as Linux-visible");
}

- (void)testPersistentRootIsNotDocumentsTruth {
    NSString *persistentRoot = [NSString stringWithUTF8String:vfs_persistent_backing_root()];

    XCTAssertFalse([persistentRoot containsString:@"/Documents"],
                   @"persistent backing root must not treat Documents as Linux root truth");
    XCTAssertFalse(path_is_own_sandbox("/Documents/example.txt"),
                   @"Documents path fragments must not become Linux root truth through sandbox heuristics");
}

- (void)testPersistentBackingRootReverseTranslation {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    int ret;

    ret = vfs_translate_path("/etc/passwd", host_path, sizeof(host_path));
    XCTAssertEqual(ret, 0, @"persistent path translation should succeed");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    XCTAssertEqual(ret, 0, @"persistent host path should reverse translate");
    XCTAssertEqualObjects([NSString stringWithUTF8String:virtual_path], @"/etc/passwd",
                          @"persistent host path should map back to /etc/passwd");
}

- (void)testCacheBackingRootReverseTranslation {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    int ret;

    ret = vfs_translate_path("/var/cache/x", host_path, sizeof(host_path));
    XCTAssertEqual(ret, 0, @"cache path translation should succeed");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    XCTAssertEqual(ret, 0, @"cache host path should reverse translate");
    XCTAssertEqualObjects([NSString stringWithUTF8String:virtual_path], @"/var/cache/x",
                          @"cache host path should map back to /var/cache/x");
}

- (void)testTempBackingRootReverseTranslation {
    char host_path[MAX_PATH];
    char virtual_path[MAX_PATH];
    int ret;

    ret = vfs_translate_path("/tmp/x", host_path, sizeof(host_path));
    XCTAssertEqual(ret, 0, @"temp path translation should succeed");

    ret = vfs_reverse_translate(host_path, virtual_path, sizeof(virtual_path));
    XCTAssertEqual(ret, 0, @"temp host path should reverse translate");
    XCTAssertEqualObjects([NSString stringWithUTF8String:virtual_path], @"/tmp/x",
                          @"temp host path should map back to /tmp/x");
}

- (void)testParentEscapeIsRejected {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("../secret", host_path, sizeof(host_path));

    XCTAssertEqual(ret, -EINVAL, @"parent escapes should be rejected");
}

/* ============================================================================
 * TASK-AWARE PATH RESOLUTION TESTS
 * ============================================================================ */

- (void)testTaskAwareAbsolutePathUsesVirtualRoot {
    /* Create an fs_struct with custom root */
    struct fs_struct *fs = alloc_fs_struct();
    XCTAssertTrue(fs != NULL, @"fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/etc");

    /* Absolute path should resolve from root - with root="/", "/bin/ls" -> "/bin/ls" */
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_task("/bin/ls", host_path, sizeof(host_path), fs);

    XCTAssertEqual(ret, 0, @"absolute path translation should succeed");
    NSString *result = [NSString stringWithUTF8String:host_path];
    NSString *expected = [NSString stringWithFormat:@"%s/bin/ls", vfs_host_backing_root()];
    XCTAssertEqualObjects(result, expected, @"absolute path should resolve from virtual root");

    free_fs_struct(fs);
}

- (void)testTaskAwareRelativePathUsesPwd {
    /* Create an fs_struct with custom pwd */
    struct fs_struct *fs = alloc_fs_struct();
    XCTAssertTrue(fs != NULL, @"fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/etc");

    /* Relative path should resolve from pwd */
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_task("passwd", host_path, sizeof(host_path), fs);

    XCTAssertEqual(ret, 0, @"relative path translation should succeed");
    NSString *result = [NSString stringWithUTF8String:host_path];
    NSString *expected = [NSString stringWithFormat:@"%s/etc/passwd", vfs_host_backing_root()];
    XCTAssertEqualObjects(result, expected, @"relative path should resolve from virtual pwd");

    free_fs_struct(fs);
}

- (void)testTaskAwareRelativePathWithSubdirectories {
    /* Create an fs_struct with nested pwd */
    struct fs_struct *fs = alloc_fs_struct();
    XCTAssertTrue(fs != NULL, @"fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/usr/local");

    /* Relative path with subdirectory */
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_task("bin/myapp", host_path, sizeof(host_path), fs);

    XCTAssertEqual(ret, 0, @"nested relative path translation should succeed");
    NSString *result = [NSString stringWithUTF8String:host_path];
    NSString *expected = [NSString stringWithFormat:@"%s/usr/local/bin/myapp", vfs_host_backing_root()];
    XCTAssertEqualObjects(result, expected, @"relative path should resolve correctly from nested pwd");

    free_fs_struct(fs);
}

- (void)testTaskAwareParentEscapeRejected {
    /* Create an fs_struct */
    struct fs_struct *fs = alloc_fs_struct();
    XCTAssertTrue(fs != NULL, @"fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/");
    fs_init_pwd(fs, "/etc");

    /* Parent escape should be rejected even with task context */
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_task("../secret", host_path, sizeof(host_path), fs);

    XCTAssertEqual(ret, -EINVAL, @"parent escapes should be rejected with task context");

    free_fs_struct(fs);
}

- (void)testTaskAwareAbsolutePathUsesTaskRootPrefix {
    struct fs_struct *fs = alloc_fs_struct();
    XCTAssertTrue(fs != NULL, @"fs_struct allocation should succeed");
    if (!fs) return;

    fs_init_root(fs, "/sandbox");
    fs_init_pwd(fs, "/sandbox/work");

    char host_path[MAX_PATH];
    int ret = vfs_translate_path_task("/bin/ls", host_path, sizeof(host_path), fs);

    XCTAssertEqual(ret, 0, @"absolute path translation should succeed from non-root task root");
    NSString *result = [NSString stringWithUTF8String:host_path];
    NSString *expected = [NSString stringWithFormat:@"%s/sandbox/bin/ls", vfs_host_backing_root()];
    XCTAssertEqualObjects(result, expected, @"absolute paths should resolve from task root prefix");

    free_fs_struct(fs);
}

- (void)testGetcwdMatchesTaskPwdAndRelativeResolution {
    struct task_struct *originalTask = get_current();
    struct task_struct *task = alloc_task();
    XCTAssertTrue(task != NULL, @"task allocation should succeed");
    if (!task) return;

    task->fs = alloc_fs_struct();
    XCTAssertTrue(task->fs != NULL, @"fs_struct allocation should succeed");
    if (!task->fs) {
        free_task(task);
        return;
    }

    fs_init_root(task->fs, "/");
    fs_init_pwd(task->fs, "/usr/local");
    set_current(task);

    char cwd[MAX_PATH];
    char resolved[MAX_PATH];
    char expected[MAX_PATH];

    char *cwd_result = getcwd_impl(cwd, sizeof(cwd));
    XCTAssertTrue(cwd_result != NULL, @"getcwd_impl should return current task pwd");
    XCTAssertEqualObjects([NSString stringWithUTF8String:cwd], @"/usr/local",
                          @"getcwd_impl should report task virtual pwd");

    int resolve_ret = path_resolve("bin/tool", resolved, sizeof(resolved));
    XCTAssertEqual(resolve_ret, 0, @"path_resolve should succeed for relative task path");

    int expected_ret = vfs_translate_path_task("bin/tool", expected, sizeof(expected), task->fs);
    XCTAssertEqual(expected_ret, 0, @"expected task-aware translation should succeed");
    XCTAssertEqualObjects([NSString stringWithUTF8String:resolved], [NSString stringWithUTF8String:expected],
                          @"relative resolution should agree with task getcwd state");

    set_current(originalTask);
    free_task(task);
}

/* ============================================================================
 * DIRFD-AWARE PATH RESOLUTION TESTS
 * ============================================================================ */

- (void)testVfsTranslatePathAtUsesAtFdcwd {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_at(AT_FDCWD, "/etc/passwd", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path_at with AT_FDCWD should succeed");
    NSString *result = [NSString stringWithUTF8String:host_path];
    NSString *expected = [NSString stringWithFormat:@"%s/etc/passwd", vfs_host_backing_root()];
    XCTAssertEqualObjects(result, expected, @"AT_FDCWD should resolve from task cwd");
}

- (void)testVfsTranslatePathAtAbsolutePathIgnoresDirfd {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_at(-1, "/bin/ls", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"absolute path should succeed regardless of invalid dirfd");
    NSString *result = [NSString stringWithUTF8String:host_path];
    NSString *expected = [NSString stringWithFormat:@"%s/bin/ls", vfs_host_backing_root()];
    XCTAssertEqualObjects(result, expected, @"absolute paths should resolve from task root");
}

- (void)testVfsTranslatePathAtInvalidDirfdReturnsBadf {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path_at(9999, "relative/path", host_path, sizeof(host_path));

    XCTAssertEqual(ret, -EBADF, @"invalid dirfd should return -EBADF");
}

/* ============================================================================
 * STAT-FAMILY AND AT-FLAG SEMANTICS TESTS
 * ============================================================================ */

- (void)testVfsFstatatSupportsAtFdcwd {
    extern int vfs_contract_fstatat_at_fdcwd(void);
    XCTAssertEqual(vfs_contract_fstatat_at_fdcwd(), 0,
                   @"vfs_fstatat with AT_FDCWD should succeed");
}

- (void)testVfsFstatatSupportsSymlinkNoFollow {
    extern int vfs_contract_fstatat_symlink_nofollow(void);
    XCTAssertEqual(vfs_contract_fstatat_symlink_nofollow(), 0,
                   @"vfs_fstatat with Linux AT_SYMLINK_NOFOLLOW should succeed");
}

- (void)testOpenNoFollowRejectsSymlinkWithEloop {
    extern int vfs_contract_open_nofollow_rejects_symlink_with_eloop(void);
    XCTAssertEqual(vfs_contract_open_nofollow_rejects_symlink_with_eloop(), 0,
                   @"open with O_NOFOLLOW should reject final symlink with ELOOP, errno %d", errno);
}

- (void)testOpenatNoFollowRejectsDirfdSymlinkWithEloop {
    extern int vfs_contract_openat_nofollow_rejects_dirfd_symlink_with_eloop(void);
    XCTAssertEqual(vfs_contract_openat_nofollow_rejects_dirfd_symlink_with_eloop(), 0,
                   @"openat with O_NOFOLLOW should reject dirfd-relative final symlink with ELOOP, errno %d", errno);
}

- (void)testOpenFollowsSymlinkToFileWithoutNoFollow {
    extern int vfs_contract_open_follows_symlink_to_file(void);
    XCTAssertEqual(vfs_contract_open_follows_symlink_to_file(), 0,
                   @"open without O_NOFOLLOW should follow relative symlink to file, errno %d", errno);
}

- (void)testOpenFollowsAbsoluteSymlinkAsVirtualPath {
    extern int vfs_contract_open_follows_absolute_symlink_as_virtual_path(void);
    XCTAssertEqual(vfs_contract_open_follows_absolute_symlink_as_virtual_path(), 0,
                   @"open should interpret absolute symlink targets through the virtual root, errno %d", errno);
}

- (void)testOpenResolvesIntermediateSymlinkDirectory {
    extern int vfs_contract_open_resolves_intermediate_symlink_directory(void);
    XCTAssertEqual(vfs_contract_open_resolves_intermediate_symlink_directory(), 0,
                   @"open should resolve intermediate symlink directories in the virtual path walk, errno %d", errno);
}

- (void)testOpenSymlinkLoopReturnsEloop {
    extern int vfs_contract_open_symlink_loop_returns_eloop(void);
    XCTAssertEqual(vfs_contract_open_symlink_loop_returns_eloop(), 0,
                   @"open should reject symlink loops with ELOOP, errno %d", errno);
}

- (void)testChdirResolvesSymlinkDirectory {
    extern int vfs_contract_chdir_resolves_symlink_directory(void);
    XCTAssertEqual(vfs_contract_chdir_resolves_symlink_directory(), 0,
                   @"chdir should resolve symlink directory targets in the virtual path walk, errno %d", errno);
}

- (void)testMkdiratResolvesIntermediateSymlinkDirectory {
    extern int vfs_contract_mkdirat_resolves_intermediate_symlink_directory(void);
    XCTAssertEqual(vfs_contract_mkdirat_resolves_intermediate_symlink_directory(), 0,
                   @"mkdirat should resolve intermediate symlink directories, errno %d", errno);
}

- (void)testUnlinkatResolvesIntermediateSymlinkDirectory {
    extern int vfs_contract_unlinkat_resolves_intermediate_symlink_directory(void);
    XCTAssertEqual(vfs_contract_unlinkat_resolves_intermediate_symlink_directory(), 0,
                   @"unlinkat should resolve intermediate symlink directories, errno %d", errno);
}

- (void)testRenameatResolvesIntermediateSymlinkDirectories {
    extern int vfs_contract_renameat_resolves_intermediate_symlink_directories(void);
    XCTAssertEqual(vfs_contract_renameat_resolves_intermediate_symlink_directories(), 0,
                   @"renameat2 should resolve intermediate symlink directories, errno %d", errno);
}

- (void)testLinkatRespectsSymlinkFollowFlag {
    extern int vfs_contract_linkat_respects_symlink_follow_flag(void);
    XCTAssertEqual(vfs_contract_linkat_respects_symlink_follow_flag(), 0,
                   @"linkat should honor AT_SYMLINK_FOLLOW for final symlink targets, errno %d", errno);
}

- (void)testVfsFstatatRejectsInvalidFlags {
    struct linux_stat st;
    int ret = vfs_fstatat(AT_FDCWD, "/etc/passwd", &st, INVALID_FLAG_TEST_VALUE);

    XCTAssertEqual(ret, -EINVAL, @"vfs_fstatat should reject invalid flags");
}

- (void)testFstatInvalidFdReturnsBadf {
    struct linux_stat st;

    errno = 0;
    XCTAssertEqual(fstat_impl(-1, &st), -1, @"fstat_impl should reject invalid fd");
    XCTAssertEqual(errno, EBADF, @"fstat_impl should set EBADF for invalid fd");
}

- (void)testFstatNullStatbufReturnsFault {
    int fd = open("/etc/passwd", O_RDONLY);

    XCTAssertTrue(fd >= 0, @"open(/etc/passwd) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    XCTAssertEqual(fstat_impl(fd, NULL), -1, @"fstat_impl should reject NULL stat buffer");
    XCTAssertEqual(errno, EFAULT, @"fstat_impl should set EFAULT for NULL stat buffer");

    close(fd);
}

- (void)testFstatImplSucceedsForLinuxOwnedFd {
    struct linux_stat st;
    int fd = open("/etc/passwd", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/etc/passwd) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    XCTAssertEqual(fstat_impl(fd, &st), 0, @"fstat_impl should succeed for Linux-owned fd");
    XCTAssertTrue(st.st_mode != 0, @"fstat_impl should populate mode");
    XCTAssertTrue(st.st_nlink > 0, @"fstat_impl should populate link count");

    close(fd);
}

- (void)testFstatDuplicatedRealBackedFdMatchesOriginal {
    struct linux_stat original_st;
    struct linux_stat duplicate_st;
    int fd = open("/etc/passwd", O_RDONLY);
    int dup_fd;

    XCTAssertTrue(fd >= 0, @"open(/etc/passwd) should succeed");
    if (fd < 0) {
        return;
    }

    dup_fd = dup(fd);
    XCTAssertTrue(dup_fd >= 0, @"dup should succeed for Linux-owned fd");
    if (dup_fd < 0) {
        close(fd);
        return;
    }

    errno = 0;
    XCTAssertEqual(fstat_impl(fd, &original_st), 0, @"fstat_impl should succeed for original Linux-owned fd");
    XCTAssertEqual(fstat_impl(dup_fd, &duplicate_st), 0, @"fstat_impl should succeed for duplicated Linux-owned fd");
    XCTAssertEqual(original_st.st_dev, duplicate_st.st_dev, @"duplicated fds should preserve device identity");
    XCTAssertEqual(original_st.st_ino, duplicate_st.st_ino, @"duplicated fds should preserve inode identity");
    XCTAssertEqual(original_st.st_mode, duplicate_st.st_mode, @"duplicated fds should preserve mode bits");

    close(dup_fd);
    close(fd);
}

- (void)testFstatProcDirectoryFdReportsDirectory {
    struct linux_stat st;
    int fd = open("/proc", O_RDONLY | O_DIRECTORY, 0);

    XCTAssertTrue(fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    XCTAssertEqual(fstat_impl(fd, &st), 0, @"fstat_impl should succeed for synthetic /proc directory fd");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"synthetic /proc fd should stat as a directory");
    XCTAssertEqual(st.st_mode & 0777, 0555, @"synthetic /proc fd should preserve synthetic permissions");

    close(fd);
}

- (void)testFstatProcSelfFdDirectoryReportsDirectory {
    struct linux_stat st;
    int fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);

    XCTAssertTrue(fd >= 0, @"open(/proc/self/fd, O_DIRECTORY) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    XCTAssertEqual(fstat_impl(fd, &st), 0, @"fstat_impl should succeed for synthetic /proc/self/fd directory fd");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"synthetic /proc/self/fd fd should stat as a directory");
    XCTAssertEqual(st.st_mode & 0777, 0555, @"synthetic /proc/self/fd fd should preserve synthetic permissions");

    close(fd);
}

- (void)testFstatProcSelfFdinfoFileReportsRegularFile {
    struct linux_stat st;
    int fd = open("/proc/self/fdinfo/0", O_RDONLY, 0);

    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) {
        return;
    }

    errno = 0;
    XCTAssertEqual(fstat_impl(fd, &st), 0, @"fstat_impl should succeed for synthetic /proc/self/fdinfo file fd");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"synthetic /proc/self/fdinfo fd should stat as a regular file");
    XCTAssertEqual(st.st_mode & 0777, 0444, @"synthetic /proc/self/fdinfo fd should preserve synthetic permissions");

    close(fd);
}

- (void)testSyntheticRootStatSucceeds {
    struct linux_stat st;

    XCTAssertEqual(vfs_fstatat(AT_FDCWD, "/proc", &st, 0), 0,
                   @"synthetic root vfs_fstatat should succeed");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"/proc root should be a directory");
    XCTAssertEqual(st.st_mode & 0777, 0555, @"/proc root should have 0555 permissions");

    XCTAssertEqual(vfs_fstatat(AT_FDCWD, "/sys", &st, 0), 0,
                   @"synthetic root vfs_fstatat should succeed for /sys");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"/sys root should be a directory");

    XCTAssertEqual(vfs_fstatat(AT_FDCWD, "/dev", &st, 0), 0,
                   @"synthetic root vfs_fstatat should succeed for /dev");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"/dev root should be a directory");

    errno = 0;
    XCTAssertEqual(stat_impl("/proc", &st), 0,
                   @"public stat should succeed for synthetic root");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"public stat should return directory for /proc");

    errno = 0;
    XCTAssertEqual(lstat_impl("/sys", &st), 0,
                   @"public lstat should succeed for synthetic root");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"public lstat should return directory for /sys");
}

- (void)testSyntheticChildStatHandlesSupportedProcFilesAndRejectsUnsupportedSys {
    struct linux_stat st;
    extern int vfs_contract_fstatat_synthetic_child_nofollow(void);

    XCTAssertEqual(vfs_fstatat(AT_FDCWD, "/proc/meminfo", &st, 0), 0,
                   @"/proc/meminfo should be a supported procfs file");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"/proc/meminfo should stat as a regular file");
    XCTAssertEqual(vfs_contract_fstatat_synthetic_child_nofollow(), -ENOENT,
                   @"synthetic child vfs_fstatat with Linux AT_SYMLINK_NOFOLLOW should reject through descriptor policy");

    errno = 0;
    XCTAssertEqual(stat_impl("/proc/meminfo", &st), 0,
                   @"IXLand stat should support /proc/meminfo");

    errno = 0;
    XCTAssertEqual(lstat_impl("/sys/kernel", &st), -1,
                   @"IXLand lstat should reject unsupported synthetic child paths");
    XCTAssertEqual(errno, ENOENT, @"IXLand lstat should set ENOENT for unsupported synthetic child paths");
}

- (void)testSyntheticRootAccessSucceeds {
    XCTAssertEqual(vfs_faccessat(AT_FDCWD, "/proc", F_OK, 0), 0,
                   @"synthetic root vfs_faccessat should succeed");
    XCTAssertEqual(vfs_faccessat(AT_FDCWD, "/sys", F_OK, 0), 0,
                   @"synthetic root vfs_faccessat should succeed for /sys");
    XCTAssertEqual(vfs_faccessat(AT_FDCWD, "/dev", F_OK, 0), 0,
                   @"synthetic root vfs_faccessat should succeed for /dev");

    errno = 0;
    XCTAssertEqual(access("/proc", F_OK), 0,
                   @"public access should succeed for synthetic root");
}

- (void)testSyntheticChildAccessHandlesSupportedProcFiles {
    XCTAssertEqual(vfs_faccessat(AT_FDCWD, "/proc/meminfo", F_OK, 0), 0,
                   @"/proc/meminfo should be visible through vfs_faccessat");

    errno = 0;
    XCTAssertEqual(access("/proc/meminfo", F_OK), 0,
                   @"public access should support /proc/meminfo");
}

- (void)testSyntheticRootOpenDirectorySucceedsAndChildOpenFails {
    errno = 0;
    int proc_fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(proc_fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (proc_fd >= 0) close(proc_fd);

    errno = 0;
    int sys_fd = open("/sys", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(sys_fd >= 0, @"open(/sys, O_DIRECTORY) should succeed");
    if (sys_fd >= 0) close(sys_fd);

    errno = 0;
    int dev_fd = open("/dev", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(dev_fd >= 0, @"open(/dev, O_DIRECTORY) should succeed");
    if (dev_fd >= 0) close(dev_fd);
}

- (void)testSyntheticChildOpenHandlesSupportedProcFilesAndRejectsUnsupportedSys {
    errno = 0;
    int meminfo_fd = open("/proc/meminfo", O_RDONLY);
    XCTAssertTrue(meminfo_fd >= 0, @"public open should support /proc/meminfo");
    if (meminfo_fd >= 0) close(meminfo_fd);

    errno = 0;
    XCTAssertEqual(openat(AT_FDCWD, "/sys/kernel", O_RDONLY), -1,
                   @"public openat should reject unsupported synthetic routes before host fallback");
    XCTAssertEqual(errno, ENOTSUP, @"public openat should set ENOTSUP for unsupported synthetic routes");
}

- (void)testSyntheticRootOpenDirectorySucceeds {
    errno = 0;
    int proc_fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(proc_fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    XCTAssertEqual(errno, 0, @"errno should be 0 after open(/proc, O_DIRECTORY)");

    errno = 0;
    int sys_fd = open("/sys", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(sys_fd >= 0, @"open(/sys, O_DIRECTORY) should succeed");
    XCTAssertEqual(errno, 0, @"errno should be 0 after open(/sys, O_DIRECTORY)");

    errno = 0;
    int dev_fd = open("/dev", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(dev_fd >= 0, @"open(/dev, O_DIRECTORY) should succeed");
    XCTAssertEqual(errno, 0, @"errno should be 0 after open(/dev, O_DIRECTORY)");

    if (proc_fd >= 0) close(proc_fd);
    if (sys_fd >= 0) close(sys_fd);
    if (dev_fd >= 0) close(dev_fd);
}

- (void)testSyntheticRootGetdents64ReturnsDotAndDotdot {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    /* Ensure buffer is 8-byte aligned for struct linux_dirent64 */
    union { char storage[1024]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64(fd, buffer, sizeof(aligned.storage));
    XCTAssertTrue(nread > 0, @"getdents64(/proc) should return > 0 bytes, got %zd errno %d", nread, errno);

    // Parse the entries to verify . and ..
    bool found_dot = false;
    bool found_dotdot = false;
    size_t pos = 0;

    while (pos < (size_t)nread) {
        struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
        NSString *name = [NSString stringWithUTF8String:entry->d_name];

        if ([name isEqualToString:@"."]) {
            found_dot = true;
            XCTAssertEqual(entry->d_type, DT_DIR, @". should be DT_DIR");
        } else if ([name isEqualToString:@".."]) {
            found_dotdot = true;
            XCTAssertEqual(entry->d_type, DT_DIR, @".. should be DT_DIR");
        }

        XCTAssertTrue(entry->d_reclen > 0, @"d_reclen must be non-zero");
        XCTAssertTrue(entry->d_reclen <= (unsigned short)((size_t)nread - pos), @"d_reclen must fit remaining buffer");
        if (entry->d_reclen == 0 || entry->d_reclen > (unsigned short)((size_t)nread - pos)) {
            break;
        }
        pos += entry->d_reclen;
    }

    XCTAssertTrue(found_dot, @"getdents64(/proc) should return '.' entry");
    XCTAssertTrue(found_dotdot, @"getdents64(/proc) should return '..' entry");

    // Second call should return 0 (EOF)
    nread = getdents64(fd, buffer, sizeof(aligned.storage));
    XCTAssertEqual(nread, 0, @"Second getdents64(/proc) should return 0 (EOF)");

    close(fd);
}

- (void)testSyntheticSysAndDevGetdents64ReturnsDotAndDotdot {
    // Test /sys
    int sys_fd = open("/sys", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(sys_fd >= 0, @"open(/sys, O_DIRECTORY) should succeed");

    if (sys_fd >= 0) {
        union { char storage[1024]; uint64_t align; } aligned;
        char *buffer = aligned.storage;
        memset(buffer, 0, sizeof(aligned));

        ssize_t nread = getdents64(sys_fd, buffer, sizeof(aligned.storage));
        XCTAssertTrue(nread > 0, @"getdents64(/sys) should return > 0 bytes");

        bool found_dot = false;
        bool found_dotdot = false;
        size_t pos = 0;

        while (pos < (size_t)nread) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
            NSString *name = [NSString stringWithUTF8String:entry->d_name];

            if ([name isEqualToString:@"."]) {
                found_dot = true;
                XCTAssertEqual(entry->d_type, DT_DIR, @". should be DT_DIR");
            } else if ([name isEqualToString:@".."]) {
                found_dotdot = true;
                XCTAssertEqual(entry->d_type, DT_DIR, @".. should be DT_DIR");
            }

            XCTAssertTrue(entry->d_reclen > 0, @"d_reclen must be non-zero");
            XCTAssertTrue(entry->d_reclen <= (unsigned short)((size_t)nread - pos), @"d_reclen must fit remaining buffer");
            if (entry->d_reclen == 0 || entry->d_reclen > (unsigned short)((size_t)nread - pos)) {
                break;
            }
            pos += entry->d_reclen;
        }

        XCTAssertTrue(found_dot, @"getdents64(/sys) should return '.' entry");
        XCTAssertTrue(found_dotdot, @"getdents64(/sys) should return '..' entry");

        // Second call should return 0 (EOF)
        nread = getdents64(sys_fd, buffer, sizeof(aligned.storage));
        XCTAssertEqual(nread, 0, @"Second getdents64(/sys) should return 0 (EOF)");

        close(sys_fd);
    }

    // Test /dev
    int dev_fd = open("/dev", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(dev_fd >= 0, @"open(/dev, O_DIRECTORY) should succeed");

    if (dev_fd >= 0) {
        union { char storage[1024]; uint64_t align; } aligned;
        char *buffer = aligned.storage;
        memset(buffer, 0, sizeof(aligned));

        ssize_t nread = getdents64(dev_fd, buffer, sizeof(aligned.storage));
        XCTAssertTrue(nread > 0, @"getdents64(/dev) should return > 0 bytes");

        bool found_dot = false;
        bool found_dotdot = false;
        size_t pos = 0;

        while (pos < (size_t)nread) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + pos);
            NSString *name = [NSString stringWithUTF8String:entry->d_name];

            if ([name isEqualToString:@"."]) {
                found_dot = true;
                XCTAssertEqual(entry->d_type, DT_DIR, @". should be DT_DIR");
            } else if ([name isEqualToString:@".."]) {
                found_dotdot = true;
                XCTAssertEqual(entry->d_type, DT_DIR, @".. should be DT_DIR");
            }

            XCTAssertTrue(entry->d_reclen > 0, @"d_reclen must be non-zero");
            XCTAssertTrue(entry->d_reclen <= (unsigned short)((size_t)nread - pos), @"d_reclen must fit remaining buffer");
            if (entry->d_reclen == 0 || entry->d_reclen > (unsigned short)((size_t)nread - pos)) {
                break;
            }
            pos += entry->d_reclen;
        }

        XCTAssertTrue(found_dot, @"getdents64(/dev) should return '.' entry");
        XCTAssertTrue(found_dotdot, @"getdents64(/dev) should return '..' entry");

        // Second call should return 0 (EOF)
        nread = getdents64(dev_fd, buffer, sizeof(aligned.storage));
        XCTAssertEqual(nread, 0, @"Second getdents64(/dev) should return 0 (EOF)");

        close(dev_fd);
    }
}

/* ============================================================================
 * SYNTHETIC /dev NODE TESTS
 * ============================================================================ */

- (void)testDevNullStatSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/dev/null", &st), 0, @"stat(/dev/null) should succeed");
    XCTAssertTrue(stat_mode_is_char_device(st.st_mode), @"/dev/null should be a character device");
    XCTAssertEqual(st.st_mode & 0777, 0666, @"/dev/null should have 0666 permissions");

    errno = 0;
    XCTAssertEqual(lstat_impl("/dev/null", &st), 0, @"lstat(/dev/null) should succeed");
    XCTAssertTrue(stat_mode_is_char_device(st.st_mode), @"lstat(/dev/null) should return character device");
}

- (void)testDevZeroStatSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/dev/zero", &st), 0, @"stat(/dev/zero) should succeed");
    XCTAssertTrue(stat_mode_is_char_device(st.st_mode), @"/dev/zero should be a character device");
    XCTAssertEqual(st.st_mode & 0777, 0666, @"/dev/zero should have 0666 permissions");
}

- (void)testDevUrandomStatSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/dev/urandom", &st), 0, @"stat(/dev/urandom) should succeed");
    XCTAssertTrue(stat_mode_is_char_device(st.st_mode), @"/dev/urandom should be a character device");
    XCTAssertEqual(st.st_mode & 0777, 0666, @"/dev/urandom should have 0666 permissions");
}

- (void)testDevNullAccessSucceeds {
    errno = 0;
    XCTAssertEqual(access("/dev/null", F_OK), 0, @"access(/dev/null, F_OK) should succeed");
    XCTAssertEqual(access("/dev/null", R_OK), 0, @"access(/dev/null, R_OK) should succeed");
    XCTAssertEqual(access("/dev/null", W_OK), 0, @"access(/dev/null, W_OK) should succeed");
}

- (void)testDevZeroAccessSucceeds {
    errno = 0;
    XCTAssertEqual(access("/dev/zero", F_OK), 0, @"access(/dev/zero, F_OK) should succeed");
}

- (void)testDevUrandomAccessSucceeds {
    errno = 0;
    XCTAssertEqual(access("/dev/urandom", F_OK), 0, @"access(/dev/urandom, F_OK) should succeed");
}

- (void)testDevNullOpenSucceeds {
    errno = 0;
    int fd = open("/dev/null", O_RDWR);
    XCTAssertTrue(fd >= 0, @"open(/dev/null, O_RDWR) should succeed");
    if (fd >= 0) close(fd);
}

- (void)testDevZeroOpenSucceeds {
    errno = 0;
    int fd = open("/dev/zero", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/zero, O_RDONLY) should succeed");
    if (fd >= 0) close(fd);
}

- (void)testDevUrandomOpenSucceeds {
    errno = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/urandom, O_RDONLY) should succeed");
    if (fd >= 0) close(fd);
}

- (void)testDevNullReadReturnsEOF {
    int fd = open("/dev/null", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/null) should succeed");
    if (fd < 0) return;

    char buf[64];
    memset(buf, 0xAA, sizeof(buf));
    errno = 0;
    ssize_t nread = read(fd, buf, sizeof(buf));
    XCTAssertEqual(nread, 0, @"read(/dev/null) should return 0 (EOF)");
    close(fd);
}

- (void)testDevNullWriteSucceedsAndDiscards {
    int fd = open("/dev/null", O_WRONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/null, O_WRONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t written = write(fd, "hello", 5);
    XCTAssertEqual(written, 5, @"write(/dev/null) should succeed and report all bytes written");
    close(fd);
}

- (void)testDevZeroReadFillsZeroBytes {
    int fd = open("/dev/zero", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/zero) should succeed");
    if (fd < 0) return;

    char buf[128];
    memset(buf, 0xFF, sizeof(buf));
    errno = 0;
    ssize_t nread = read(fd, buf, sizeof(buf));
    XCTAssertEqual(nread, (ssize_t)sizeof(buf), @"read(/dev/zero) should return requested byte count");

    bool all_zero = true;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            all_zero = false;
            break;
        }
    }
    XCTAssertTrue(all_zero, @"/dev/zero read should fill buffer with zero bytes");
    close(fd);
}

- (void)testDevUrandomReadReturnsNontrivialData {
    int fd = open("/dev/urandom", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/urandom) should succeed");
    if (fd < 0) return;

    char buf[256];
    memset(buf, 0, sizeof(buf));
    errno = 0;
    ssize_t nread = read(fd, buf, sizeof(buf));
    XCTAssertEqual(nread, (ssize_t)sizeof(buf), @"read(/dev/urandom) should return requested byte count");

    bool all_zero = true;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            all_zero = false;
            break;
        }
    }
    XCTAssertFalse(all_zero, @"/dev/urandom read should return nontrivial data (not all zeros)");
    close(fd);
}

- (void)testDevZeroWriteSucceedsAndDiscards {
    int fd = open("/dev/zero", O_WRONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/zero, O_WRONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t written = write(fd, "data", 4);
    XCTAssertEqual(written, 4, @"write(/dev/zero) should succeed and discard");
    close(fd);
}

- (void)testDevUrandomWriteSucceedsAndDiscards {
    int fd = open("/dev/urandom", O_WRONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/urandom, O_WRONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t written = write(fd, "data", 4);
    XCTAssertEqual(written, 4, @"write(/dev/urandom) should succeed and discard");
    close(fd);
}

- (void)testUnsupportedDevNodeStillFails {
    errno = 0;
    struct linux_stat st;
    XCTAssertEqual(stat_impl("/dev/sda", &st), -1, @"stat(/dev/sda) should fail for unsupported dev node");
    XCTAssertEqual(errno, ENOENT, @"stat(/dev/sda) should set ENOENT");

    struct task_struct *original_task = get_current();
    struct task_struct *isolated_task = alloc_task();
    XCTAssertTrue(isolated_task != NULL, @"task allocation should succeed");
    if (!isolated_task) return;

    isolated_task->fs = alloc_fs_struct();
    XCTAssertTrue(isolated_task->fs != NULL, @"fs_struct allocation should succeed");
    if (!isolated_task->fs) {
        free_task(isolated_task);
        return;
    }

    isolated_task->signal = alloc_signal_struct();
    XCTAssertTrue(isolated_task->signal != NULL, @"signal_struct allocation should succeed");
    if (!isolated_task->signal) {
        free_task(isolated_task);
        return;
    }

    fs_init_root(isolated_task->fs, "/");
    fs_init_pwd(isolated_task->fs, "/");
    set_current(isolated_task);

    errno = 0;
    XCTAssertEqual(open("/dev/tty", O_RDONLY), -1, @"open(/dev/tty) should fail without usable controlling tty");
    XCTAssertTrue(errno == ENXIO || errno == EIO, @"open(/dev/tty) should set ENXIO (no controlling tty) or EIO (unusable controlling tty)");

    set_current(original_task);
    free_task(isolated_task);
}

- (void)testVfsFaccessatSupportsAtFdcwd {
    int ret = vfs_faccessat(AT_FDCWD, "/etc", X_OK, 0);

    XCTAssertEqual(ret, 0, @"vfs_faccessat with AT_FDCWD should succeed");
}

- (void)testVfsFaccessatRejectsInvalidFlags {
    int ret = vfs_faccessat(AT_FDCWD, "/etc", X_OK, INVALID_FLAG_TEST_VALUE);

    XCTAssertEqual(ret, -EINVAL, @"vfs_faccessat should reject invalid flags");
}

- (void)testVfsFaccessatReportsUnsupportedAtEaccess {
    extern int vfs_contract_faccessat_eaccess_returns_enotsup(void);
    XCTAssertEqual(vfs_contract_faccessat_eaccess_returns_enotsup(), -ENOTSUP,
                   @"vfs_faccessat Linux AT_EACCESS should return ENOTSUP");
}

- (void)testVfsFaccessatReportsUnsupportedSymlinkNoFollow {
    extern int vfs_contract_faccessat_symlink_nofollow_returns_enotsup(void);
    XCTAssertEqual(vfs_contract_faccessat_symlink_nofollow_returns_enotsup(), -ENOTSUP,
                   @"vfs_faccessat Linux AT_SYMLINK_NOFOLLOW should return ENOTSUP");
}

- (void)testFaccessatFollowsAbsoluteSymlinkAsVirtualPath {
    extern int vfs_contract_faccessat_follows_absolute_symlink_as_virtual_path(void);
    XCTAssertEqual(vfs_contract_faccessat_follows_absolute_symlink_as_virtual_path(), 0,
                   @"faccessat/access should follow absolute symlink targets through the virtual root, errno %d", errno);
}

- (void)testFaccessatSymlinkLoopReturnsEloop {
    extern int vfs_contract_faccessat_symlink_loop_returns_eloop(void);
    XCTAssertEqual(vfs_contract_faccessat_symlink_loop_returns_eloop(), 0,
                   @"faccessat/access should reject symlink loops with ELOOP, errno %d", errno);
}

- (void)testFsuidControlsOwnerFileAccess {
    extern int vfs_contract_fsuid_controls_owner_file_access(void);
    XCTAssertEqual(vfs_contract_fsuid_controls_owner_file_access(), 0,
                   @"fsuid should control Linux owner permission checks, errno %d", errno);
}

- (void)testFsgidControlsGroupFileAccess {
    extern int vfs_contract_fsgid_controls_group_file_access(void);
    XCTAssertEqual(vfs_contract_fsgid_controls_group_file_access(), 0,
                   @"fsgid should control Linux group permission checks, errno %d", errno);
}

- (void)testChrootRebasesAbsolutePathsAndGetcwd {
    extern int vfs_contract_chroot_rebases_absolute_paths_and_getcwd(void);
    XCTAssertEqual(vfs_contract_chroot_rebases_absolute_paths_and_getcwd(), 0,
                   @"chroot should rebase task root and getcwd through IXLand VFS, errno %d", errno);
}

- (void)testNonrootCannotChroot {
    extern int vfs_contract_nonroot_cannot_chroot(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_chroot(), 0,
                   @"non-root virtual credentials should not chroot, errno %d", errno);
}

- (void)testRootWithoutSysChrootCannotChroot {
    extern int vfs_contract_root_without_sys_chroot_cannot_chroot(void);
    XCTAssertEqual(vfs_contract_root_without_sys_chroot_cannot_chroot(), 0,
                   @"root without CAP_SYS_CHROOT should not chroot, errno %d", errno);
}

- (void)testPivotRootRebasesAbsolutePathsAndExposesOldRoot {
    extern int vfs_contract_pivot_root_rebases_absolute_paths_and_exposes_old_root(void);
    XCTAssertEqual(vfs_contract_pivot_root_rebases_absolute_paths_and_exposes_old_root(), 0,
                   @"pivot_root should rebase absolute paths and expose the old root, errno %d", errno);
}

- (void)testFchdirUpdatesVirtualPwd {
    extern int vfs_contract_fchdir_updates_virtual_pwd(void);
    XCTAssertEqual(vfs_contract_fchdir_updates_virtual_pwd(), 0,
                   @"fchdir should update IXLand task pwd without host cwd, errno %d", errno);
}

- (void)testBindMountRedirectsTargetTree {
    extern int vfs_contract_bind_mount_redirects_target_tree(void);
    XCTAssertEqual(vfs_contract_bind_mount_redirects_target_tree(), 0,
                   @"MS_BIND mount should redirect target tree through IXLand VFS, errno %d", errno);
}

- (void)testBindMountDuplicateTargetReturnsBusy {
    extern int vfs_contract_bind_mount_duplicate_target_returns_busy(void);
    XCTAssertEqual(vfs_contract_bind_mount_duplicate_target_returns_busy(), 0,
                   @"duplicate bind mount target should return EBUSY, errno %d", errno);
}

- (void)testUmountRestoresTargetTree {
    extern int vfs_contract_umount_restores_target_tree(void);
    XCTAssertEqual(vfs_contract_umount_restores_target_tree(), 0,
                   @"umount should restore target tree path resolution, errno %d", errno);
}

- (void)testBindMountRejectsNonBindMount {
    extern int vfs_contract_bind_mount_rejects_non_bind_mount(void);
    XCTAssertEqual(vfs_contract_bind_mount_rejects_non_bind_mount(), 0,
                   @"non-bind mount should remain unsupported without host mount semantics, errno %d", errno);
}

- (void)testMountNamespaceSharedAcrossTaskDup {
    extern int vfs_contract_mount_namespace_shared_across_task_dup(void);
    XCTAssertEqual(vfs_contract_mount_namespace_shared_across_task_dup(), 0,
                   @"duplicated task fs state should share the virtual mount namespace, errno %d", errno);
}

- (void)testMountNamespaceUnshareIsolatesChildMounts {
    extern int vfs_contract_mount_namespace_unshare_isolates_child_mounts(void);
    XCTAssertEqual(vfs_contract_mount_namespace_unshare_isolates_child_mounts(), 0,
                   @"unshared task mount namespace should isolate child bind mounts, errno %d", errno);
}

- (void)testProcSelfMountinfoListsBindMount {
    extern int vfs_contract_proc_self_mountinfo_lists_bind_mount(void);
    XCTAssertEqual(vfs_contract_proc_self_mountinfo_lists_bind_mount(), 0,
                   @"/proc/self/mountinfo should expose virtual bind mounts, errno %d", errno);
}

- (void)testProcSelfMountinfoUsesLinuxShapedOptionalFields {
    extern int vfs_contract_proc_self_mountinfo_uses_linux_shaped_optional_fields(void);
    XCTAssertEqual(vfs_contract_proc_self_mountinfo_uses_linux_shaped_optional_fields(), 0,
                   @"/proc/self/mountinfo should use Linux-shaped root/source/mount option fields, errno %d", errno);
}

- (void)testProcSelfMountinfoReportsSharedPropagation {
    extern int vfs_contract_proc_self_mountinfo_reports_shared_propagation(void);
    XCTAssertEqual(vfs_contract_proc_self_mountinfo_reports_shared_propagation(), 0,
                   @"/proc/self/mountinfo should report shared propagation metadata, errno %d", errno);
}

- (void)testProcSelfMountinfoReportsSlavePrivateAndUnbindablePropagation {
    extern int vfs_contract_proc_self_mountinfo_reports_slave_private_and_unbindable_propagation(void);
    XCTAssertEqual(vfs_contract_proc_self_mountinfo_reports_slave_private_and_unbindable_propagation(), 0,
                   @"/proc/self/mountinfo should report Linux-shaped propagation metadata, errno %d", errno);
}

- (void)testMountRejectsMultiplePropagationFlags {
    extern int vfs_contract_mount_rejects_multiple_propagation_flags(void);
    XCTAssertEqual(vfs_contract_mount_rejects_multiple_propagation_flags(), 0,
                   @"mount should reject multiple propagation flags, errno %d", errno);
}

- (void)testUnprivilegedMountOperationsFailWithoutNamespaceMutation {
    extern int vfs_contract_unprivileged_mount_operations_fail_without_namespace_mutation(void);
    XCTAssertEqual(vfs_contract_unprivileged_mount_operations_fail_without_namespace_mutation(), 0,
                   @"unprivileged mount operations should fail without mutating the virtual mount namespace, errno %d", errno);
}

- (void)testSharedMountPropagatesChildBindToPeer {
    extern int vfs_contract_shared_mount_propagates_child_bind_to_peer(void);
    XCTAssertEqual(vfs_contract_shared_mount_propagates_child_bind_to_peer(), 0,
                   @"shared mount peers should receive child bind propagation, errno %d", errno);
}

- (void)testSharedMountinfoUsesPeerGroupIds {
    extern int vfs_contract_shared_mountinfo_uses_peer_group_ids(void);
    XCTAssertEqual(vfs_contract_shared_mountinfo_uses_peer_group_ids(), 0,
                   @"/proc/self/mountinfo should report deterministic shared peer groups, errno %d", errno);
}

- (void)testSharedMountPropagatesNestedChildBindToPeer {
    extern int vfs_contract_shared_mount_propagates_nested_child_bind_to_peer(void);
    XCTAssertEqual(vfs_contract_shared_mount_propagates_nested_child_bind_to_peer(), 0,
                   @"shared mount peers should recursively receive nested bind propagation, errno %d", errno);
}

- (void)testSharedMountUnmountPropagatesNestedChildFromPeer {
    extern int vfs_contract_shared_mount_unmount_propagates_nested_child_from_peer(void);
    XCTAssertEqual(vfs_contract_shared_mount_unmount_propagates_nested_child_from_peer(), 0,
                   @"shared mount peers should receive nested unmount propagation, errno %d", errno);
}

- (void)testRecursiveUnmountPropagatesNestedChildrenFromSharedPeer {
    extern int vfs_contract_recursive_umount_propagates_nested_children_from_shared_peer(void);
    XCTAssertEqual(vfs_contract_recursive_umount_propagates_nested_children_from_shared_peer(), 0,
                   @"recursive unmount should propagate nested child unmounts from shared peer trees, errno %d", errno);
}

- (void)testHardlinkInodeMetadataSurvivesUnlink {
    extern int vfs_contract_hardlink_inode_metadata_survives_unlink(void);
    XCTAssertEqual(vfs_contract_hardlink_inode_metadata_survives_unlink(), 0,
                   @"hardlink inode metadata should sync across aliases and survive unlink, errno %d", errno);
}

- (void)testProcFdMarksOpenUnlinkedFileDeleted {
    extern int vfs_contract_proc_fd_marks_open_unlinked_file_deleted(void);
    XCTAssertEqual(vfs_contract_proc_fd_marks_open_unlinked_file_deleted(), 0,
                   @"/proc/self/fd should preserve open unlinked file identity, errno %d", errno);
}

- (void)testPrivateChildUnmountDoesNotPropagateToSharedPeer {
    extern int vfs_contract_private_child_unmount_does_not_propagate_to_shared_peer(void);
    XCTAssertEqual(vfs_contract_private_child_unmount_does_not_propagate_to_shared_peer(), 0,
                   @"private child unmount should not propagate to shared peer trees, errno %d", errno);
}

- (void)testCloneNewnsSharedPropagationStaysInsideChildNamespace {
    extern int vfs_contract_clone_newns_shared_propagation_stays_inside_child_namespace(void);
    XCTAssertEqual(vfs_contract_clone_newns_shared_propagation_stays_inside_child_namespace(), 0,
                   @"CLONE_NEWNS propagation should stay within the child mount namespace, errno %d", errno);
}

- (void)testMountNamespaceRefsTrackTaskLifecycle {
    extern int vfs_contract_mount_namespace_refs_track_task_lifecycle(void);
    XCTAssertEqual(vfs_contract_mount_namespace_refs_track_task_lifecycle(), 0,
                   @"mount namespace refs should track cloned task lifetime, errno %d", errno);
}

- (void)testCloneNewnsRebasesSharedPeerGroups {
    extern int vfs_contract_clone_newns_rebases_shared_peer_groups(void);
    XCTAssertEqual(vfs_contract_clone_newns_rebases_shared_peer_groups(), 0,
                   @"CLONE_NEWNS should rebase shared peer group ids inside the cloned mount namespace, errno %d", errno);
}

- (void)testCloneNewnsRebasesSlaveMasterToChildPeerGroup {
    extern int vfs_contract_clone_newns_rebases_slave_master_to_child_peer_group(void);
    XCTAssertEqual(vfs_contract_clone_newns_rebases_slave_master_to_child_peer_group(), 0,
                   @"CLONE_NEWNS should rebase slave masters to child peer groups, errno %d", errno);
}

- (void)testUnmountBusyWhenOpenFdPinsMountTree {
    extern int vfs_contract_umount_busy_when_open_fd_pins_mount_tree(void);
    XCTAssertEqual(vfs_contract_umount_busy_when_open_fd_pins_mount_tree(), 0,
                   @"normal unmount should return EBUSY while an open fd pins the mount tree, errno %d", errno);
}

- (void)testLazyUnmountDetachesBusyMountFromNamespace {
    extern int vfs_contract_lazy_umount_detaches_busy_mount_from_namespace(void);
    XCTAssertEqual(vfs_contract_lazy_umount_detaches_busy_mount_from_namespace(), 0,
                   @"lazy unmount should detach a busy mount from the namespace, errno %d", errno);
}

- (void)testLazyUnmountRemovesBusyMountFromProcMountinfo {
    extern int vfs_contract_lazy_umount_removes_busy_mount_from_proc_mountinfo(void);
    XCTAssertEqual(vfs_contract_lazy_umount_removes_busy_mount_from_proc_mountinfo(), 0,
                   @"lazy unmount should remove a busy detached mount from proc mountinfo, errno %d", errno);
}

- (void)testUnmountExpireRequiresMarkThenUnmount {
    extern int vfs_contract_umount_expire_requires_mark_then_unmount(void);
    XCTAssertEqual(vfs_contract_umount_expire_requires_mark_then_unmount(), 0,
                   @"expiry unmount should require one mark pass before detaching, errno %d", errno);
}

- (void)testLazyUnmountReclaimsDetachedRefAfterPinRelease {
    extern int vfs_contract_lazy_umount_reclaims_detached_ref_after_pin_release(void);
    XCTAssertEqual(vfs_contract_lazy_umount_reclaims_detached_ref_after_pin_release(), 0,
                   @"lazy unmount should retain and later reclaim detached mount refs, errno %d", errno);
}

- (void)testUnmountBusyWhenPwdPinsMountTree {
    extern int vfs_contract_umount_busy_when_pwd_pins_mount_tree(void);
    XCTAssertEqual(vfs_contract_umount_busy_when_pwd_pins_mount_tree(), 0,
                   @"normal unmount should return EBUSY while cwd pins the mount tree, errno %d", errno);
}

- (void)testUnmountBusyWhenRootPinsMountTree {
    extern int vfs_contract_umount_busy_when_root_pins_mount_tree(void);
    XCTAssertEqual(vfs_contract_umount_busy_when_root_pins_mount_tree(), 0,
                   @"normal unmount should return EBUSY while a task root pins the mount tree, errno %d", errno);
}

- (void)testSlaveMountReceivesNestedChildFromSharedMaster {
    extern int vfs_contract_slave_mount_receives_nested_child_from_shared_master(void);
    XCTAssertEqual(vfs_contract_slave_mount_receives_nested_child_from_shared_master(), 0,
                   @"slave mounts should receive nested propagation from their shared master, errno %d", errno);
}

- (void)testMountSetattrRecursiveMarksChildPrivate {
    extern int vfs_contract_mount_setattr_recursive_marks_child_private(void);
    XCTAssertEqual(vfs_contract_mount_setattr_recursive_marks_child_private(), 0,
                   @"mount_setattr AT_RECURSIVE should update the virtual mount subtree, errno %d", errno);
}

- (void)testRecursiveRemountPrivateMarksChildPrivate {
    extern int vfs_contract_recursive_remount_private_marks_child_private(void);
    XCTAssertEqual(vfs_contract_recursive_remount_private_marks_child_private(), 0,
                   @"MS_REC propagation remount should update the virtual mount subtree, errno %d", errno);
}

- (void)testRecursiveRemountSlavePreservesPeerGroupMasters {
    extern int vfs_contract_recursive_remount_slave_preserves_peer_group_masters(void);
    XCTAssertEqual(vfs_contract_recursive_remount_slave_preserves_peer_group_masters(), 0,
                   @"MS_REC slave remount should preserve former shared peer groups as masters, errno %d", errno);
}

- (void)testListmountStatmountReportsSlaveMaster {
    extern int vfs_contract_listmount_statmount_reports_slave_master(void);
    XCTAssertEqual(vfs_contract_listmount_statmount_reports_slave_master(), 0,
                   @"listmount/statmount should expose slave master propagation metadata, errno %d", errno);
}

- (void)testMountinfoReportsNestedParentMountId {
    extern int vfs_contract_mountinfo_reports_nested_parent_mount_id(void);
    XCTAssertEqual(vfs_contract_mountinfo_reports_nested_parent_mount_id(), 0,
                   @"/proc/self/mountinfo should report nested parent mount ids, errno %d", errno);
}

- (void)testRecursiveBindClonesNestedMountTopology {
    extern int vfs_contract_recursive_bind_clones_nested_mount_topology(void);
    XCTAssertEqual(vfs_contract_recursive_bind_clones_nested_mount_topology(), 0,
                   @"MS_REC bind should clone nested mount topology, errno %d", errno);
}

- (void)testMoveMountRelocatesBindSubtree {
    extern int vfs_contract_move_mount_relocates_bind_subtree(void);
    XCTAssertEqual(vfs_contract_move_mount_relocates_bind_subtree(), 0,
                   @"MS_MOVE should relocate a virtual mount subtree, errno %d", errno);
}

- (void)testOpenTreeCloneReturnsMountFdVisibleInProc {
    extern int vfs_contract_open_tree_clone_returns_mount_fd_visible_in_proc(void);
    XCTAssertEqual(vfs_contract_open_tree_clone_returns_mount_fd_visible_in_proc(), 0,
                   @"open_tree should return a virtual mount fd visible through /proc/self/fd, errno %d", errno);
}

- (void)testMoveMountAttachesOpenTreeClone {
    extern int vfs_contract_move_mount_attaches_open_tree_clone(void);
    XCTAssertEqual(vfs_contract_move_mount_attaches_open_tree_clone(), 0,
                   @"move_mount should attach an open_tree clone fd into the virtual mount namespace, errno %d", errno);
}

- (void)testOpenTreeCloneSurvivesSourceUnmountUntilAttached {
    extern int vfs_contract_open_tree_clone_survives_source_unmount_until_attached(void);
    XCTAssertEqual(vfs_contract_open_tree_clone_survives_source_unmount_until_attached(), 0,
                   @"open_tree clone fd should survive source unmount until attached, errno %d", errno);
}

- (void)testOpenTreeCloneNestedMountTopologyAttachesRecursively {
    extern int vfs_contract_open_tree_clone_nested_mount_topology_attaches_recursively(void);
    XCTAssertEqual(vfs_contract_open_tree_clone_nested_mount_topology_attaches_recursively(), 0,
                   @"open_tree clone should preserve nested mount topology when attached, errno %d", errno);
}

- (void)testSharedMountMovePropagatesToPeer {
    extern int vfs_contract_shared_mount_move_propagates_to_peer(void);
    XCTAssertEqual(vfs_contract_shared_mount_move_propagates_to_peer(), 0,
                   @"move_mount should propagate moved shared child mounts to peer mount trees, errno %d", errno);
}

- (void)testCloneNewnsMovePropagatesToRebasedSlaveReceiver {
    extern int vfs_contract_clone_newns_move_propagates_to_rebased_slave_receiver(void);
    XCTAssertEqual(vfs_contract_clone_newns_move_propagates_to_rebased_slave_receiver(), 0,
                   @"move_mount should propagate inside cloned namespaces to rebased slave receivers, errno %d", errno);
}

- (void)testRecursiveUmountPropagatesNestedSharedSubtree {
    extern int vfs_contract_recursive_umount_propagates_nested_shared_subtree(void);
    XCTAssertEqual(vfs_contract_recursive_umount_propagates_nested_shared_subtree(), 0,
                   @"recursive unmount should propagate nested shared subtree removal, errno %d", errno);
}

- (void)testMountIdsStableAcrossMoveUnmountAndNamespaceClone {
    extern int vfs_contract_mount_ids_stable_across_move_unmount_and_namespace_clone(void);
    XCTAssertEqual(vfs_contract_mount_ids_stable_across_move_unmount_and_namespace_clone(), 0,
                   @"virtual mount ids should survive move, sibling unmount, and namespace clone, errno %d", errno);
}

- (void)testMountNamespaceTeardownAccountsMountsAndDetachedRefs {
    extern int vfs_contract_mount_namespace_teardown_accounts_mounts_and_detached_refs(void);
    XCTAssertEqual(vfs_contract_mount_namespace_teardown_accounts_mounts_and_detached_refs(), 0,
                   @"mount namespace teardown should account active mounts and detached refs, errno %d", errno);
}

- (void)testMountNamespaceDropReclaimsChildDetachedRefs {
    extern int vfs_contract_mount_namespace_drop_reclaims_child_detached_refs(void);
    XCTAssertEqual(vfs_contract_mount_namespace_drop_reclaims_child_detached_refs(), 0,
                   @"dropping a child mount namespace should reclaim its detached refs, errno %d", errno);
}

- (void)testLazyUmountRefSurvivesDescendantTaskTree {
    extern int vfs_contract_lazy_umount_ref_survives_descendant_task_tree(void);
    XCTAssertEqual(vfs_contract_lazy_umount_ref_survives_descendant_task_tree(), 0,
                   @"lazy detached refs should survive descendant task pins and reap after task release, errno %d", errno);
}

- (void)testLazyUmountRefSurvivesChildRootAndPwdPins {
    extern int vfs_contract_lazy_umount_ref_survives_child_root_and_pwd_pins(void);
    XCTAssertEqual(vfs_contract_lazy_umount_ref_survives_child_root_and_pwd_pins(), 0,
                   @"lazy detached refs should survive child root/cwd pins and reap after task release, errno %d", errno);
}

- (void)testChildMountNamespaceDetachSurvivesChildRootAndPwdPins {
    extern int vfs_contract_child_mount_namespace_detach_survives_child_root_and_pwd_pins(void);
    XCTAssertEqual(vfs_contract_child_mount_namespace_detach_survives_child_root_and_pwd_pins(), 0,
                   @"child mount namespace detached refs should survive child root/cwd pins and reap after child release, errno %d", errno);
}

- (void)testLazyDetachPropagatesNestedSharedSlaveTree {
    extern int vfs_contract_lazy_detach_propagates_nested_shared_slave_tree(void);
    XCTAssertEqual(vfs_contract_lazy_detach_propagates_nested_shared_slave_tree(), 0,
                   @"lazy detach should propagate nested shared/slave mount removal without leaking detached refs, errno %d", errno);
}

- (void)testUmount2DetachDetachesBusyMountFromNamespace {
    extern int vfs_contract_umount2_detach_detaches_busy_mount_from_namespace(void);
    XCTAssertEqual(vfs_contract_umount2_detach_detaches_busy_mount_from_namespace(), 0,
                   @"umount2 MNT_DETACH should detach busy mount from virtual namespace, errno %d", errno);
}

- (void)testUmount2RejectsUnusedLinuxUmountFlag {
    extern int vfs_contract_umount2_rejects_unused_linux_umount_flag(void);
    XCTAssertEqual(vfs_contract_umount2_rejects_unused_linux_umount_flag(), 0,
                   @"umount2 should reject the Linux UMOUNT_UNUSED flag and leave the mount visible, errno %d", errno);
}

- (void)testUmount2ForceDetachesBusyMountAndReapsAfterPinRelease {
    extern int vfs_contract_umount2_force_detaches_busy_mount_and_reaps_after_pin_release(void);
    XCTAssertEqual(vfs_contract_umount2_force_detaches_busy_mount_and_reaps_after_pin_release(), 0,
                   @"umount2 MNT_FORCE should detach busy virtual mounts and reap after pins release, errno %d", errno);
}

- (void)testForceUmountDetachedRefsAreMountNamespaceScoped {
    extern int vfs_contract_force_umount_detached_refs_are_mount_namespace_scoped(void);
    XCTAssertEqual(vfs_contract_force_umount_detached_refs_are_mount_namespace_scoped(), 0,
                   @"detached mount refs should be scoped to the mount namespace that was detached, errno %d", errno);
}

- (void)testForceUmountPropagatesSharedSlaveSubtreeTeardown {
    extern int vfs_contract_force_umount_propagates_shared_slave_subtree_teardown(void);
    XCTAssertEqual(vfs_contract_force_umount_propagates_shared_slave_subtree_teardown(), 0,
                   @"MNT_FORCE subtree teardown should propagate across shared peers and slave receivers, errno %d", errno);
}

- (void)testUmount2ExpireRequiresMarkThenUnmount {
    extern int vfs_contract_umount2_expire_requires_mark_then_unmount(void);
    XCTAssertEqual(vfs_contract_umount2_expire_requires_mark_then_unmount(), 0,
                   @"umount2 MNT_EXPIRE should mark then unmount on second call, errno %d", errno);
}

- (void)testUmount2RejectsExpireWithDetach {
    extern int vfs_contract_umount2_rejects_expire_with_detach(void);
    XCTAssertEqual(vfs_contract_umount2_rejects_expire_with_detach(), 0,
                   @"umount2 should reject MNT_EXPIRE combined with MNT_DETACH, errno %d", errno);
}

- (void)testUmount2NofollowRejectsSymlinkTarget {
    extern int vfs_contract_umount2_nofollow_rejects_symlink_target(void);
    XCTAssertEqual(vfs_contract_umount2_nofollow_rejects_symlink_target(), 0,
                   @"umount2 UMOUNT_NOFOLLOW should reject symlink mount targets, errno %d", errno);
}

- (void)testProcSelfMountsListsBindMount {
    extern int vfs_contract_proc_self_mounts_lists_bind_mount(void);
    XCTAssertEqual(vfs_contract_proc_self_mounts_lists_bind_mount(), 0,
                   @"/proc/self/mounts should expose virtual bind mounts, errno %d", errno);
}

- (void)testBindMountRemountReadonlyRejectsWrites {
    extern int vfs_contract_bind_mount_remount_readonly_rejects_writes(void);
    XCTAssertEqual(vfs_contract_bind_mount_remount_readonly_rejects_writes(), 0,
                   @"read-only virtual bind mounts should reject writes with EROFS, errno %d", errno);
}

- (void)testBindMountRemountReadwritePermitsWrites {
    extern int vfs_contract_bind_mount_remount_readwrite_permits_writes(void);
    XCTAssertEqual(vfs_contract_bind_mount_remount_readwrite_permits_writes(), 0,
                   @"read-write remount should permit writes through the virtual bind mount, errno %d", errno);
}

- (void)testProcSelfMountinfoReportsReadonlyRemount {
    extern int vfs_contract_proc_self_mountinfo_reports_readonly_remount(void);
    XCTAssertEqual(vfs_contract_proc_self_mountinfo_reports_readonly_remount(), 0,
                   @"/proc/self/mountinfo should report read-only remounted bind mounts, errno %d", errno);
}

- (void)testNonrootCannotCreateBindMount {
    extern int vfs_contract_nonroot_cannot_create_bind_mount(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_create_bind_mount(), 0,
                   @"non-root virtual credentials should not create bind mounts, errno %d", errno);
}

- (void)testRootWithoutSysAdminCannotCreateBindMount {
    extern int vfs_contract_root_without_sys_admin_cannot_create_bind_mount(void);
    XCTAssertEqual(vfs_contract_root_without_sys_admin_cannot_create_bind_mount(), 0,
                   @"root without CAP_SYS_ADMIN should not create bind mounts, errno %d", errno);
}

- (void)testNonrootCannotUnmountBindMount {
    extern int vfs_contract_nonroot_cannot_unmount_bind_mount(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_unmount_bind_mount(), 0,
                   @"non-root virtual credentials should not unmount bind mounts, errno %d", errno);
}

- (void)testProcSelfMountinfoUsesCurrentMountNamespace {
    extern int vfs_contract_proc_self_mountinfo_uses_current_mount_namespace(void);
    XCTAssertEqual(vfs_contract_proc_self_mountinfo_uses_current_mount_namespace(), 0,
                   @"/proc/self/mountinfo should follow current task mount namespace, errno %d", errno);
}

- (void)testProcSelfMountViewsDoNotExposeHostPaths {
    extern int vfs_contract_proc_self_mount_views_do_not_expose_host_paths(void);
    XCTAssertEqual(vfs_contract_proc_self_mount_views_do_not_expose_host_paths(), 0,
                   @"proc mount views should not expose host backing paths, errno %d", errno);
}

- (void)testNonrootCannotReadRootPrivateFile {
    extern int vfs_contract_nonroot_cannot_read_root_private_file(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_read_root_private_file(), 0,
                   @"non-root virtual credentials should not read root-private files, errno %d", errno);
}

- (void)testNonrootCanReadOtherReadableFile {
    extern int vfs_contract_nonroot_can_read_other_readable_file(void);
    XCTAssertEqual(vfs_contract_nonroot_can_read_other_readable_file(), 0,
                   @"other-readable files should remain readable by non-root credentials, errno %d", errno);
}

- (void)testNonrootCreatedFileRecordsVirtualOwner {
    extern int vfs_contract_nonroot_created_file_records_virtual_owner(void);
    XCTAssertEqual(vfs_contract_nonroot_created_file_records_virtual_owner(), 0,
                   @"created files should record the current virtual euid as owner, errno %d", errno);
}

- (void)testNonrootCannotUnlinkInsideRootPrivateDir {
    extern int vfs_contract_nonroot_cannot_unlink_inside_root_private_dir(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_unlink_inside_root_private_dir(), 0,
                   @"non-root virtual credentials should not unlink inside root-private directories, errno %d", errno);
}

- (void)testNonrootCannotOpenThroughUnsearchableParentDirectory {
    extern int vfs_contract_nonroot_cannot_open_through_unsearchable_parent_directory(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_open_through_unsearchable_parent_directory(), 0,
                   @"non-root should need execute permission on parent directories, errno %d", errno);
}

- (void)testStickyDirectoryBlocksNonOwnerUnlink {
    extern int vfs_contract_sticky_directory_blocks_nonowner_unlink(void);
    XCTAssertEqual(vfs_contract_sticky_directory_blocks_nonowner_unlink(), 0,
                   @"sticky directories should block unlink by non-owners, errno %d", errno);
}

- (void)testNonrootCannotMkdiratInsideRootPrivateDir {
    extern int vfs_contract_nonroot_cannot_mkdirat_inside_root_private_dir(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_mkdirat_inside_root_private_dir(), 0,
                   @"mkdirat should enforce virtual parent permissions after credential changes, errno %d", errno);
}

- (void)testNonrootCannotUnlinkatInsideRootPrivateDir {
    extern int vfs_contract_nonroot_cannot_unlinkat_inside_root_private_dir(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_unlinkat_inside_root_private_dir(), 0,
                   @"unlinkat should enforce virtual parent permissions after credential changes, errno %d", errno);
}

- (void)testLinkatUsesVirtualDirfds {
    extern int vfs_contract_linkat_uses_virtual_dirfds(void);
    XCTAssertEqual(vfs_contract_linkat_uses_virtual_dirfds(), 0,
                   @"linkat should resolve source and target through virtual dirfds, errno %d", errno);
}

- (void)testSymlinkatAndReadlinkatUseVirtualDirfds {
    extern int vfs_contract_symlinkat_and_readlinkat_use_virtual_dirfds(void);
    XCTAssertEqual(vfs_contract_symlinkat_and_readlinkat_use_virtual_dirfds(), 0,
                   @"symlinkat/readlinkat should resolve through virtual dirfds, errno %d", errno);
}

- (void)testRenameat2ExchangeSwapsFilesThroughVirtualDirfds {
    extern int vfs_contract_renameat2_exchange_swaps_files_through_virtual_dirfds(void);
    XCTAssertEqual(vfs_contract_renameat2_exchange_swaps_files_through_virtual_dirfds(), 0,
                   @"renameat2 exchange should swap files through virtual dirfds, errno %d", errno);
}

- (void)testRenameat2ExchangeSwapsVirtualMetadata {
    extern int vfs_contract_renameat2_exchange_swaps_virtual_metadata(void);
    XCTAssertEqual(vfs_contract_renameat2_exchange_swaps_virtual_metadata(), 0,
                   @"renameat2 exchange should swap virtual metadata with files, errno %d", errno);
}

- (void)testRenameat2NoreplaceExistingTargetReturnsExist {
    extern int vfs_contract_renameat2_noreplace_existing_target_returns_exist(void);
    XCTAssertEqual(vfs_contract_renameat2_noreplace_existing_target_returns_exist(), 0,
                   @"renameat2 noreplace should fail with EEXIST and leave files unchanged, errno %d", errno);
}

- (void)testRenameatOverwriteMovesVirtualMetadata {
    extern int vfs_contract_renameat_overwrite_moves_virtual_metadata(void);
    XCTAssertEqual(vfs_contract_renameat_overwrite_moves_virtual_metadata(), 0,
                   @"rename overwrite should move source virtual metadata to target, errno %d", errno);
}

- (void)testRenameDirectoryOverNonemptyDirectoryReturnsNotempty {
    extern int vfs_contract_rename_directory_over_nonempty_directory_returns_notempty(void);
    XCTAssertEqual(vfs_contract_rename_directory_over_nonempty_directory_returns_notempty(), 0,
                   @"rename directory over non-empty directory should return ENOTEMPTY, errno %d", errno);
}

- (void)testRenameFileOverDirectoryReturnsIsdir {
    extern int vfs_contract_rename_file_over_directory_returns_isdir(void);
    XCTAssertEqual(vfs_contract_rename_file_over_directory_returns_isdir(), 0,
                   @"rename file over directory should return EISDIR, errno %d", errno);
}

- (void)testRenameDirectoryOverFileReturnsNotdir {
    extern int vfs_contract_rename_directory_over_file_returns_notdir(void);
    XCTAssertEqual(vfs_contract_rename_directory_over_file_returns_notdir(), 0,
                   @"rename directory over file should return ENOTDIR, errno %d", errno);
}

- (void)testRootChownUpdatesVirtualOwner {
    extern int vfs_contract_root_chown_updates_virtual_owner(void);
    XCTAssertEqual(vfs_contract_root_chown_updates_virtual_owner(), 0,
                   @"root chown should update virtual owner metadata, errno %d", errno);
}

- (void)testNonrootCannotChownOwnedFile {
    extern int vfs_contract_nonroot_cannot_chown_owned_file(void);
    XCTAssertEqual(vfs_contract_nonroot_cannot_chown_owned_file(), 0,
                   @"non-root chown should fail through virtual credential checks, errno %d", errno);
}

- (void)testOwnerChmodUpdatesVirtualMode {
    extern int vfs_contract_owner_chmod_updates_virtual_mode(void);
    XCTAssertEqual(vfs_contract_owner_chmod_updates_virtual_mode(), 0,
                   @"file owner chmod should update virtual mode metadata, errno %d", errno);
}

- (void)testNonownerCannotChmodFile {
    extern int vfs_contract_nonowner_cannot_chmod_file(void);
    XCTAssertEqual(vfs_contract_nonowner_cannot_chmod_file(), 0,
                   @"non-owner chmod should fail through virtual credential checks, errno %d", errno);
}

- (void)testFchmodUpdatesVirtualMode {
    extern int vfs_contract_fchmod_updates_virtual_mode(void);
    XCTAssertEqual(vfs_contract_fchmod_updates_virtual_mode(), 0,
                   @"fchmod should update virtual mode metadata, errno %d", errno);
}

- (void)testFchownUpdatesVirtualOwner {
    extern int vfs_contract_fchown_updates_virtual_owner(void);
    XCTAssertEqual(vfs_contract_fchown_updates_virtual_owner(), 0,
                   @"fchown should update virtual owner metadata, errno %d", errno);
}

- (void)testSupplementaryGroupCanReadGroupFile {
    extern int vfs_contract_supplementary_group_can_read_group_file(void);
    XCTAssertEqual(vfs_contract_supplementary_group_can_read_group_file(), 0,
                   @"supplementary group membership should grant group read permission, errno %d", errno);
}

- (void)testMissingSupplementaryGroupCannotReadGroupFile {
    extern int vfs_contract_missing_supplementary_group_cannot_read_group_file(void);
    XCTAssertEqual(vfs_contract_missing_supplementary_group_cannot_read_group_file(), 0,
                   @"missing supplementary group membership should not grant group read permission, errno %d", errno);
}

- (void)testRootWithoutDacCapsCannotReadPrivateFile {
    extern int vfs_contract_root_without_dac_caps_cannot_read_private_file(void);
    XCTAssertEqual(vfs_contract_root_without_dac_caps_cannot_read_private_file(), 0,
                   @"clearing virtual DAC capabilities should make root obey file mode permissions, errno %d", errno);
}

- (void)testStatfsReportsVirtualProcAndTmpfs {
    extern int vfs_contract_statfs_reports_virtual_proc_and_tmpfs(void);
    XCTAssertEqual(vfs_contract_statfs_reports_virtual_proc_and_tmpfs(), 0,
                   @"statfs/fstatfs should report IXLand virtual filesystem state, errno %d", errno);
}

/* ============================================================================
 * SIGNAL-FAMILY SEMANTICS TESTS
 * ============================================================================ */

- (void)testSignalKillSucceeds {
    pid_t pid = getpid();
    int ret = kill(pid, 0);
    XCTAssertEqual(ret, 0, @"kill(self, 0) should succeed");
}

/* ============================================================================
 * PROC/SELF ABSTRACTION TESTS
 * ============================================================================ */

- (void)testProcSelfStatSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/proc/self", &st), 0, @"stat(/proc/self) should succeed");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"/proc/self should be a directory");
}

- (void)testProcSelfStatFileSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/proc/self/stat", &st), 0, @"stat(/proc/self/stat) should succeed");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"/proc/self/stat should be a regular file");
    XCTAssertEqual(st.st_mode & 0777, 0444, @"/proc/self/stat should have 0444 permissions");
}

- (void)testProcSelfCmdlineSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/proc/self/cmdline", &st), 0, @"stat(/proc/self/cmdline) should succeed");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"/proc/self/cmdline should be a regular file");
    XCTAssertEqual(st.st_mode & 0777, 0444, @"/proc/self/cmdline should have 0444 permissions");
}

- (void)testProcSelfCommSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/proc/self/comm", &st), 0, @"stat(/proc/self/comm) should succeed");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"/proc/self/comm should be a regular file");
    XCTAssertEqual(st.st_mode & 0777, 0444, @"/proc/self/comm should have 0444 permissions");
}

- (void)testProcSelfStatmSucceeds {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/proc/self/statm", &st), 0, @"stat(/proc/self/statm) should succeed");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"/proc/self/statm should be a regular file");
    XCTAssertEqual(st.st_mode & 0777, 0444, @"/proc/self/statm should have 0444 permissions");
}

- (void)testProcSelfExeIsSymlink {
    struct linux_stat st;
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"current task should exist");
    if (!task) return;

    char original_exe[MAX_PATH];
    strncpy(original_exe, task->exe, sizeof(original_exe) - 1);
    original_exe[sizeof(original_exe) - 1] = '\0';

    strncpy(task->exe, "/bin/test-proc-self-exe", sizeof(task->exe) - 1);
    task->exe[sizeof(task->exe) - 1] = '\0';

    errno = 0;
    int ret = lstat_impl("/proc/self/exe", &st);
    XCTAssertEqual(ret, 0, @"lstat(/proc/self/exe) should succeed when exe is seeded");
    XCTAssertTrue(stat_mode_is_symlink(st.st_mode), @"/proc/self/exe should be a symlink");

    strncpy(task->exe, original_exe, sizeof(task->exe) - 1);
    task->exe[sizeof(task->exe) - 1] = '\0';
}

- (void)testProcSelfCwdIsSymlink {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(lstat_impl("/proc/self/cwd", &st), 0, @"lstat(/proc/self/cwd) should succeed");
    XCTAssertTrue(stat_mode_is_symlink(st.st_mode), @"/proc/self/cwd should be a symlink");
}

- (void)testProcSelfFdIsDirectory {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/proc/self/fd", &st), 0, @"stat(/proc/self/fd) should succeed");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"/proc/self/fd should be a directory");
}

- (void)testProcSelfFdinfoIsDirectory {
    struct linux_stat st;
    errno = 0;
    XCTAssertEqual(stat_impl("/proc/self/fdinfo", &st), 0, @"stat(/proc/self/fdinfo) should succeed");
    XCTAssertTrue(stat_mode_is_directory(st.st_mode), @"/proc/self/fdinfo should be a directory");
}

/* ============================================================================
 * /PROC/SELF/FD/ SYMLINK TESTS
 * ============================================================================ */

- (void)testProcSelfFdSymlinksExist {
    struct linux_stat st;

    // Get stdin fd (0) info via /proc/self/fd/0
    errno = 0;
    int ret = lstat_impl("/proc/self/fd/0", &st);
    XCTAssertEqual(ret, 0, @"lstat(/proc/self/fd/0) should succeed");
    XCTAssertTrue(stat_mode_is_symlink(st.st_mode), @"/proc/self/fd/0 should be a symlink");

    // Get stdout fd (1) info via /proc/self/fd/1
    errno = 0;
    ret = lstat_impl("/proc/self/fd/1", &st);
    XCTAssertEqual(ret, 0, @"lstat(/proc/self/fd/1) should succeed");
    XCTAssertTrue(stat_mode_is_symlink(st.st_mode), @"/proc/self/fd/1 should be a symlink");

    // Get stderr fd (2) info via /proc/self/fd/2
    errno = 0;
    ret = lstat_impl("/proc/self/fd/2", &st);
    XCTAssertEqual(ret, 0, @"lstat(/proc/self/fd/2) should succeed");
    XCTAssertTrue(stat_mode_is_symlink(st.st_mode), @"/proc/self/fd/2 should be a symlink");
}

- (void)testProcSelfFdSymlinksPointToValidPaths {
    char link_target[MAX_PATH];
    ssize_t len;

    // Read symlink target for fd 0
    len = readlink("/proc/self/fd/0", link_target, sizeof(link_target) - 1);
    XCTAssertTrue(len > 0, @"readlink(/proc/self/fd/0) should return a path");
    if (len > 0) {
        link_target[len] = '\0';
        XCTAssertTrue(strlen(link_target) > 0, @"fd 0 should point to a valid path");
    }
}

- (void)testProcSelfFdSymlinksReflectActualFdState {
    // Create a temporary file to get a real fd
    int test_fd = vfs_path_contract_open_tmp_fd_symlink_file();
    XCTAssertTrue(test_fd >= 0, @"open should succeed for test file");
    if (test_fd < 0) return;

    // Construct the /proc/self/fd path for this fd
    char fd_path[64];
    NSString *fdPathString = [NSString stringWithFormat:@"/proc/self/fd/%d", test_fd];
    const char *fdPathUTF8 = [fdPathString UTF8String];
    XCTAssertNotNil(fdPathString, @"fd path string should be created");
    XCTAssertNotEqual(fdPathUTF8, NULL, @"fd path UTF8 conversion should succeed");
    if (fdPathUTF8 == NULL) {
        close(test_fd);
        return;
    }
    size_t fdPathLength = strlen(fdPathUTF8);
    XCTAssertTrue(fdPathLength < sizeof(fd_path), @"fd path should fit fixed-size buffer");
    if (fdPathLength >= sizeof(fd_path)) {
        close(test_fd);
        return;
    }
    memcpy(fd_path, fdPathUTF8, fdPathLength + 1);

    // Verify the symlink exists and points to the expected path
    char link_target[MAX_PATH];
    ssize_t len = readlink(fd_path, link_target, sizeof(link_target) - 1);

    XCTAssertTrue(len > 0, @"readlink should succeed for newly created fd");

    if (len > 0) {
        link_target[len] = '\0';
        // Should contain "test_fd_symlink" somewhere in the path
        XCTAssertTrue(strstr(link_target, "test_fd_symlink") != NULL,
                      @"fd symlink should point to the test file");
    }

    // Clean up
    close(test_fd);
    unlink("/tmp/test_fd_symlink");
}

- (void)testProcSelfFdInvalidFdNumbersFail {
    struct linux_stat st;

    // Try to stat a non-existent fd
    errno = 0;
    int ret = stat_impl("/proc/self/fd/999", &st);
    XCTAssertEqual(ret, -1, @"stat(/proc/self/fd/999) should fail for invalid fd");
    XCTAssertEqual(errno, ENOENT, @"errno should be ENOENT for invalid fd");

    // Try to stat a non-numeric fd "name"
    errno = 0;
    ret = stat_impl("/proc/self/fd/abc", &st);
    XCTAssertEqual(ret, -1, @"stat(/proc/self/fd/abc) should fail for non-numeric fd");
    XCTAssertEqual(errno, ENOENT, @"errno should be ENOENT for non-numeric fd");
}

- (void)testProcSelfFdinfoFilesExist {
    struct linux_stat st;

    // fdinfo/0 should exist and be a regular file
    errno = 0;
    int ret = stat_impl("/proc/self/fdinfo/0", &st);
    XCTAssertEqual(ret, 0, @"stat(/proc/self/fdinfo/0) should succeed");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"/proc/self/fdinfo/0 should be a regular file");
    XCTAssertEqual(st.st_mode & 0777, 0444, @"/proc/self/fdinfo/0 should have 0444 permissions");

    // fdinfo/1 should also exist
    errno = 0;
    ret = stat_impl("/proc/self/fdinfo/1", &st);
    XCTAssertEqual(ret, 0, @"stat(/proc/self/fdinfo/1) should succeed");
    XCTAssertTrue(stat_mode_is_regular(st.st_mode), @"/proc/self/fdinfo/1 should be a regular file");
}

- (void)testProcSelfFdinfoInvalidFdNumbersFail {
    struct linux_stat st;

    // Try to stat a non-existent fdinfo entry
    errno = 0;
    int ret = stat_impl("/proc/self/fdinfo/999", &st);
    XCTAssertEqual(ret, -1, @"stat(/proc/self/fdinfo/999) should fail for invalid fdinfo entry");
    XCTAssertEqual(errno, ENOENT, @"errno should be ENOENT for invalid fdinfo entry");

    // Try to stat a non-numeric fdinfo entry
    errno = 0;
    ret = stat_impl("/proc/self/fdinfo/abc", &st);
    XCTAssertEqual(ret, -1, @"stat(/proc/self/fdinfo/abc) should fail for non-numeric fdinfo entry");
    XCTAssertEqual(errno, ENOENT, @"errno should be ENOENT for non-numeric fdinfo entry");
}


/* ============================================================================
 * HOST PATH VALIDATION AND FAILURE CASES
 * ============================================================================ */

- (void)testVfsTranslatePathRejectsAbsoluteHostPath {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/private/var/mobile/test", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"current vfs_translate_path behavior routes host-absolute inputs through virtual resolution");
}

- (void)testVfsTranslatePathRejectsNonRoutePath {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/nonexistent/path", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path should fallback to persistent route for unknown paths");
}

- (void)testVfsTranslatePathRejectsInvalidVirtualPath {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"current vfs_translate_path behavior accepts empty path through virtual resolution");
}

- (void)testVfsTranslatePathRejectsNullPath {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path(NULL, host_path, sizeof(host_path));

    XCTAssertEqual(ret, -EINVAL, @"vfs_translate_path should reject NULL path");
}

- (void)testVfsTranslatePathRejectsNullBuffer {
    int ret = vfs_translate_path("/etc/passwd", NULL, MAX_PATH);

    XCTAssertEqual(ret, -EINVAL, @"vfs_translate_path should reject NULL buffer");
}

- (void)testVfsTranslatePathRejectsZeroBufferSize {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/passwd", host_path, 0);

    XCTAssertEqual(ret, -EINVAL, @"vfs_translate_path should reject zero buffer size");
}

/* ============================================================================
 * SYNTHETIC /dev/tty TESTS
 * ============================================================================ */

- (void)testDevTtyOpenFailsWithoutControllingTty {
    struct task_struct *original_task = get_current();
    struct task_struct *isolated_task = alloc_task();
    XCTAssertTrue(isolated_task != NULL, @"task allocation should succeed");
    if (!isolated_task) return;

    isolated_task->fs = alloc_fs_struct();
    XCTAssertTrue(isolated_task->fs != NULL, @"fs_struct allocation should succeed");
    if (!isolated_task->fs) {
        free_task(isolated_task);
        return;
    }

    isolated_task->signal = alloc_signal_struct();
    XCTAssertTrue(isolated_task->signal != NULL, @"signal_struct allocation should succeed");
    if (!isolated_task->signal) {
        free_task(isolated_task);
        return;
    }

    fs_init_root(isolated_task->fs, "/");
    fs_init_pwd(isolated_task->fs, "/");
    set_current(isolated_task);

    errno = 0;
    int fd = open("/dev/tty", O_RDWR);
    XCTAssertEqual(fd, -1, @"open(/dev/tty) should fail without controlling tty");
    XCTAssertTrue(errno == ENXIO || errno == EIO, @"open(/dev/tty) should set ENXIO or EIO");

    set_current(original_task);
    free_task(isolated_task);
}

- (void)testDevTtyStatFails {
    struct linux_stat st;
    errno = 0;
    int ret = stat_impl("/dev/tty", &st);
    XCTAssertEqual(ret, 0, @"stat(/dev/tty) should report the devfs character node");
    XCTAssertTrue(stat_mode_is_char_device(st.st_mode), @"/dev/tty should be a character device");
}

- (void)testDevTtyAccessFails {
    errno = 0;
    int ret = access("/dev/tty", F_OK);

    XCTAssertEqual(ret, 0, @"access(/dev/tty) should report the devfs node even without a controlling tty");
}

/* ============================================================================
 * PERSISTENT FILESYSTEM TESTS
 * ============================================================================ */

/* ============================================================================
 * BUFFER SIZE VALIDATION TESTS
 * ============================================================================ */

- (void)testVfsTranslatePathRejectsTooSmallBuffer {
    char small_buf[1];
    int ret = vfs_translate_path("/etc/passwd", small_buf, sizeof(small_buf));

    XCTAssertEqual(ret, -ENAMETOOLONG, @"vfs_translate_path should reject too-small buffer");
}

- (void)testPathResolveRejectsNullPath {
    char resolved[MAX_PATH];
    errno = 0;
    int ret = path_resolve(NULL, resolved, sizeof(resolved));

    XCTAssertEqual(ret, -1, @"path_resolve should reject NULL path");
    XCTAssertEqual(errno, EINVAL, @"path_resolve should set EINVAL for NULL path");
}

- (void)testPathResolveRejectsNullBuffer {
    errno = 0;
    int ret = path_resolve("/etc/passwd", NULL, MAX_PATH);

    XCTAssertEqual(ret, -1, @"path_resolve should reject NULL buffer");
    XCTAssertEqual(errno, EINVAL, @"path_resolve should set EINVAL for NULL buffer");
}

- (void)testPathResolveRejectsZeroBufferSize {
    char resolved[MAX_PATH];
    errno = 0;
    int ret = path_resolve("/etc/passwd", resolved, 0);

    XCTAssertEqual(ret, -1, @"path_resolve should reject zero buffer size");
    XCTAssertEqual(errno, EINVAL, @"path_resolve should set EINVAL for zero buffer size");
}

/* ============================================================================
 * SYNTHETIC FD READ/PREAD/LSEEK/GETDENTS64 TESTS
 * ============================================================================ */

- (void)testGetdents64HostBackedDirectoryDoesNotCorruptFdLifecycle {
    /* Open a real host-backed directory */
    int fd = open("/etc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(fd >= 0, @"open(/etc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    /* Read directory entries */
    union { char storage[4096]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64(fd, buffer, sizeof(aligned.storage));
    XCTAssertTrue(nread > 0, @"getdents64(/etc) should return > 0 bytes");

    /* Close the directory */
    close(fd);

    /* Open another file - should not get EBADF from fd reuse corruption */
    int fd2 = open("/etc/passwd", O_RDONLY);
    XCTAssertTrue(fd2 >= 0, @"open(/etc/passwd) should succeed after getdents64/close cycle");
    if (fd2 >= 0) {
        char buf[64];
        ssize_t n = read(fd2, buf, sizeof(buf));
        XCTAssertTrue(n >= 0, @"read should succeed on reopened fd");
        close(fd2);
    }
}

- (void)testGetdents64ProcSelfFdListsOpenFd {
    /* Create a known fd to be listed */
    int test_fd = open("/dev/null", O_RDONLY);
    XCTAssertTrue(test_fd >= 0, @"open(/dev/null) should succeed");
    if (test_fd < 0) return;

    int proc_fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(proc_fd >= 0, @"open(/proc/self/fd, O_DIRECTORY) should succeed");
    if (proc_fd < 0) {
        close(test_fd);
        return;
    }

    union { char storage[4096]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64(proc_fd, buffer, sizeof(aligned.storage));
    XCTAssertTrue(nread > 0, @"getdents64(/proc/self/fd) should return > 0 bytes");

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

    XCTAssertTrue(found_test_fd, @"getdents64(/proc/self/fd) should list the open test fd");

    close(proc_fd);
    close(test_fd);
}

- (void)testGetdents64ProcSelfFdinfoListsOpenFd {
    /* Create a known fd to be listed */
    int test_fd = open("/dev/null", O_RDONLY);
    XCTAssertTrue(test_fd >= 0, @"open(/dev/null) should succeed");
    if (test_fd < 0) return;

    int proc_fd = open("/proc/self/fdinfo", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(proc_fd >= 0, @"open(/proc/self/fdinfo, O_DIRECTORY) should succeed");
    if (proc_fd < 0) {
        close(test_fd);
        return;
    }

    union { char storage[4096]; uint64_t align; } aligned;
    char *buffer = aligned.storage;
    memset(buffer, 0, sizeof(aligned));

    ssize_t nread = getdents64(proc_fd, buffer, sizeof(aligned.storage));
    XCTAssertTrue(nread > 0, @"getdents64(/proc/self/fdinfo) should return > 0 bytes");

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
            XCTAssertEqual(entry->d_type, DT_REG, @"fdinfo entries should be regular files");
            break;
        }
        if (entry->d_reclen == 0) break;
        pos += entry->d_reclen;
    }

    XCTAssertTrue(found_test_fd, @"getdents64(/proc/self/fdinfo) should list the open test fd");

    close(proc_fd);
    close(test_fd);
}

- (void)testReadProcSelfFdinfoAdvancesOffset {
    int fd = open_impl("/proc/self/fdinfo/0", O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    /* First read should get some content */
    char buf1[64];
    memset(buf1, 0, sizeof(buf1));
    ssize_t n1 = read(fd, buf1, sizeof(buf1));
    XCTAssertTrue(n1 > 0, @"read should return content from /proc/self/fdinfo/0");

    /* Second read should get EOF since buffer was larger than content */
    char buf2[64];
    ssize_t n2 = read(fd, buf2, sizeof(buf2));
    (void)n2; /* n2 should be 0 (EOF) since fdinfo content is small */

    close(fd);
}

- (void)testPreadProcSelfFdinfoDoesNotAdvanceOffset {
    int source_fd = open_impl("/dev/null", O_RDONLY, 0);
    XCTAssertTrue(source_fd >= 0, @"open(/dev/null) should succeed");
    if (source_fd < 0) return;
    int target_fd = dup2_impl(source_fd, 42);
    close_impl(source_fd);
    XCTAssertEqual(target_fd, 42, @"dup2 to stable fd should succeed");
    if (target_fd != 42) return;

    int fd = open_impl("/proc/self/fdinfo/42", O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/<fd>) should succeed");
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
    XCTAssertTrue(pread_n > 0, @"pread at offset 0 should return content");

    /* Now read from current position (should be at 0 since pread doesn't advance) */
    char read_buf[64];
    memset(read_buf, 0, sizeof(read_buf));
    ssize_t read_n = read_impl(fd, read_buf, sizeof(read_buf));
    XCTAssertTrue(read_n > 0, @"read after pread should return content from offset 0");

    close_impl(fd);
    close_impl(target_fd);
}

- (void)testPreadProcSelfFdinfoOffsetAtEofReturnsZero {
    int source_fd = open_impl("/dev/null", O_RDONLY, 0);
    XCTAssertTrue(source_fd >= 0, @"open(/dev/null) should succeed");
    if (source_fd < 0) return;
    int target_fd = dup2_impl(source_fd, 42);
    close_impl(source_fd);
    XCTAssertEqual(target_fd, 42, @"dup2 to stable fd should succeed");
    if (target_fd != 42) return;

    int fd = open_impl("/proc/self/fdinfo/42", O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/<fd>) should succeed");
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
    XCTAssertEqual(pread_n, 0, @"pread at EOF should return 0");

    pread_n = pread_impl(fd, buf, sizeof(buf), total + 100);
    XCTAssertEqual(pread_n, 0, @"pread past EOF should return 0");

    close_impl(fd);
    close_impl(target_fd);
}

- (void)testPreadProcSelfFdinfoNullBufferReturnsFault {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pread(fd, NULL, 64, 0);
    XCTAssertEqual(n, -1, @"pread with NULL buffer should return -1");
    XCTAssertEqual(errno, EFAULT, @"pread with NULL buffer should set EFAULT");

    close(fd);
}

- (void)testPreadSyntheticDirectoryReturnsDirectoryError {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    XCTAssertEqual(n, -1, @"pread on directory should return -1");
    XCTAssertEqual(errno, EISDIR, @"pread on directory should set EISDIR");

    close(fd);
}

- (void)testLseekProcSelfFdinfoPolicyIsExplicit {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    /* Current policy: synthetic proc files return ESPIPE for lseek */
    errno = 0;
    off_t result = lseek(fd, 0, SEEK_SET);
    XCTAssertEqual(result, (off_t)-1, @"lseek on /proc/self/fdinfo should return -1");
    XCTAssertEqual(errno, ESPIPE, @"lseek on synthetic proc file should set ESPIPE");

    errno = 0;
    result = lseek(fd, 0, SEEK_CUR);
    XCTAssertEqual(result, (off_t)-1, @"lseek(SEEK_CUR) on /proc/self/fdinfo should return -1");
    XCTAssertEqual(errno, ESPIPE, @"lseek on synthetic proc file should set ESPIPE");

    errno = 0;
    result = lseek(fd, 0, SEEK_END);
    XCTAssertEqual(result, (off_t)-1, @"lseek(SEEK_END) on /proc/self/fdinfo should return -1");
    XCTAssertEqual(errno, ESPIPE, @"lseek on synthetic proc file should set ESPIPE");

    close(fd);
}

/* ============================================================================
 * FD ACCESS MODE TESTS
 * ============================================================================ */

- (void)testReadDevNullWriteOnlyFdReturnsBadf {
    int fd = open("/dev/null", O_WRONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/null, O_WRONLY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = read(fd, buf, sizeof(buf));
    XCTAssertEqual(n, -1, @"read on write-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"read on write-only fd should set EBADF");

    close(fd);
}

- (void)testReadDevZeroWriteOnlyFdReturnsBadf {
    int fd = open("/dev/zero", O_WRONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/zero, O_WRONLY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = read(fd, buf, sizeof(buf));
    XCTAssertEqual(n, -1, @"read on write-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"read on write-only fd should set EBADF");

    close(fd);
}

- (void)testWriteDevNullReadOnlyFdReturnsBadf {
    int fd = open("/dev/null", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/null, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    XCTAssertEqual(n, -1, @"write on read-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"write on read-only fd should set EBADF");

    close(fd);
}

- (void)testWriteDevZeroReadOnlyFdReturnsBadf {
    int fd = open("/dev/zero", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/zero, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    XCTAssertEqual(n, -1, @"write on read-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"write on read-only fd should set EBADF");

    close(fd);
}

- (void)testWriteDevUrandomReadOnlyFdReturnsBadf {
    int fd = open("/dev/urandom", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/urandom, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    XCTAssertEqual(n, -1, @"write on read-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"write on read-only fd should set EBADF");

    close(fd);
}

- (void)testOpenProcSelfFdinfoWriteOnlyReturnsAcces {
    /* Opening proc files write-only should fail at open time */
    errno = 0;
    int fd = open("/proc/self/fdinfo/0", O_WRONLY);
    XCTAssertEqual(fd, -1, @"open(/proc/self/fdinfo/0, O_WRONLY) should fail");
    XCTAssertEqual(errno, EACCES, @"open write-only on proc file should set EACCES");
}

- (void)testWriteProcSelfFdinfoReadOnlyFdReturnsBadf {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    XCTAssertEqual(n, -1, @"write on read-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"write on read-only fd should set EBADF");

    close(fd);
}

- (void)testPreadProcSelfFdinfoReadOnlyFdWorks {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    XCTAssertTrue(n > 0, @"pread on readable fd should succeed");

    close(fd);
}

- (void)testPreadDevNullWriteOnlyFdReturnsBadf {
    int fd = open("/dev/null", O_WRONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/null, O_WRONLY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    XCTAssertEqual(n, -1, @"pread on write-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"pread on write-only fd should set EBADF");

    close(fd);
}

- (void)testPwriteDevNullReadOnlyFdReturnsBadf {
    int fd = open("/dev/null", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/dev/null, O_RDONLY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pwrite(fd, "hello", 5, 0);
    XCTAssertEqual(n, -1, @"pwrite on read-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"pwrite on read-only fd should set EBADF");

    close(fd);
}

- (void)testPwriteProcSelfFdinfoReadOnlyFdDoesNotHostFallback {
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open(/proc/self/fdinfo/0) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pwrite(fd, "hello", 5, 0);
    /* Should fail with EBADF (access mode) not with EBADF from host fallback on -1 fd */
    XCTAssertEqual(n, -1, @"pwrite on read-only fd should return -1");
    XCTAssertEqual(errno, EBADF, @"pwrite on read-only fd should set EBADF");

    close(fd);
}

- (void)testReadSyntheticDirectoryReturnsEisdir {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = read(fd, buf, sizeof(buf));
    XCTAssertEqual(n, -1, @"read on directory should return -1");
    XCTAssertEqual(errno, EISDIR, @"read on directory should set EISDIR");

    close(fd);
}

- (void)testWriteSyntheticDirectoryReturnsEisdir {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = write(fd, "hello", 5);
    XCTAssertEqual(n, -1, @"write on directory should return -1");
    XCTAssertEqual(errno, EISDIR, @"write on directory should set EISDIR");

    close(fd);
}

- (void)testPwriteSyntheticDirectoryReturnsEisdir {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    errno = 0;
    ssize_t n = pwrite(fd, "hello", 5, 0);
    XCTAssertEqual(n, -1, @"pwrite on directory should return -1");
    XCTAssertEqual(errno, EISDIR, @"pwrite on directory should set EISDIR");

    close(fd);
}

- (void)testPreadSyntheticDirectoryReturnsEisdir {
    int fd = open("/proc", O_RDONLY | O_DIRECTORY);
    XCTAssertTrue(fd >= 0, @"open(/proc, O_DIRECTORY) should succeed");
    if (fd < 0) return;

    char buf[64];
    errno = 0;
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    XCTAssertEqual(n, -1, @"pread on directory should return -1");
    XCTAssertEqual(errno, EISDIR, @"pread on directory should set EISDIR");

    close(fd);
}

@end
