/*
 * Host bridge tests for VFS path operations that require host helpers,
 * direct host-backed file setup, or HostTestSupport bridge shims.
 */

#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "IXLandHostAdapter/fs/backing_io_decls.h"
#include "IXLandHostAdapter/fs/path_host.h"
#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "HostTestSupport.h"

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
extern int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags);
extern int stat_impl(const char *path, struct linux_stat *statbuf);
extern int lstat_impl(const char *path, struct linux_stat *statbuf);

static int vfs_test_open_host_path(const char *path, int flags, unsigned int mode) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return (int)syscall(SYS_open_nocancel, path, flags, (mode_t)mode);
#pragma clang diagnostic pop
}

static void vfs_test_translate_virtual_path_or_fail(const char *path, char *host_path, size_t host_path_len) {
    int ret = vfs_translate_path(path, host_path, host_path_len);
    XCTAssertEqual(ret, 0, @"path should translate for %s", path);
}

static void vfs_test_ensure_host_directory_exists(const char *host_path, const char *context_path) {
    NSString *directory = [NSString stringWithUTF8String:host_path];
    NSError *error = nil;
    BOOL ok = [[NSFileManager defaultManager] createDirectoryAtPath:directory
                                        withIntermediateDirectories:YES
                                                         attributes:nil
                                                              error:&error];
    XCTAssertTrue(ok, @"directory setup should succeed for %s (%@)", context_path, error);
}

static void vfs_test_ensure_virtual_parent_directory(const char *path) {
    char host_path[MAX_PATH];
    vfs_test_translate_virtual_path_or_fail(path, host_path, sizeof(host_path));

    char *last_slash = strrchr(host_path, '/');
    if (last_slash == NULL) {
        return;
    }
    *last_slash = '\0';
    vfs_test_ensure_host_directory_exists(host_path, path);
}

static void vfs_test_ensure_virtual_directory_exists(const char *path) {
    char host_path[MAX_PATH];
    vfs_test_translate_virtual_path_or_fail(path, host_path, sizeof(host_path));
    vfs_test_ensure_host_directory_exists(host_path, path);
}

static void vfs_test_remove_linux_path(const char *path) {
    char host_path[MAX_PATH];
    vfs_test_translate_virtual_path_or_fail(path, host_path, sizeof(host_path));

    if (host_unlink_impl(host_path) < 0 && errno != ENOENT) {
        int dir_ret = host_rmdir_impl(host_path);
        XCTAssertTrue(dir_ret == 0 || errno == ENOENT, @"cleanup should tolerate missing path for %s", path);
    }
}

static int vfs_test_open_host_directory_fd(const char *host_path) {
    return vfs_test_open_host_path(host_path, O_RDONLY | O_DIRECTORY, 0);
}

static void vfs_test_seed_linux_file(const char *path) {
    char host_path[MAX_PATH];
    vfs_test_translate_virtual_path_or_fail(path, host_path, sizeof(host_path));
    vfs_test_ensure_virtual_parent_directory(path);

    int fd = vfs_test_open_host_path(host_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    XCTAssertTrue(fd >= 0, @"file seed should succeed for %s", path);
    if (fd < 0) {
        return;
    }

    static const char payload[] = "seed";
    ssize_t written = host_write_impl(fd, payload, sizeof(payload) - 1);
    XCTAssertEqual(written, (ssize_t)(sizeof(payload) - 1), @"seed write should succeed for %s", path);
    host_close_impl(fd);
}

@interface VFSPathHostBridgeTests : XCTestCase
@end

@implementation VFSPathHostBridgeTests

- (void)testHostStatMissingPathReturnsLinuxEnoent_HostBacked {
    struct linux_stat st;
    int ret = host_stat_impl("/definitely/missing/ixland-host-stat", &st);

    XCTAssertEqual(ret, -ENOENT, @"host_stat_impl should return Linux -ENOENT for missing path");
}

- (void)testHostLstatMissingPathReturnsLinuxEnoent_HostBacked {
    struct linux_stat st;
    int ret = host_lstat_impl("/definitely/missing/ixland-host-lstat", &st);

    XCTAssertEqual(ret, -ENOENT, @"host_lstat_impl should return Linux -ENOENT for missing path");
}

- (void)testHostAccessMissingPathReturnsLinuxEnoent_HostBacked {
    int ret = host_access_impl("/definitely/missing/ixland-host-access", F_OK);

    XCTAssertEqual(ret, -ENOENT, @"host_access_impl should return Linux -ENOENT for missing path");
}

- (void)testVirtualEtcPasswdExists_HostBacked {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/passwd", host_path, sizeof(host_path));
    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/passwd should succeed");

    int fd = vfs_test_open_host_path(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host open /etc/passwd should succeed");
    if (fd >= 0) host_close_impl(fd);
}

- (void)testHostFstatReturnsLinuxEbadfForInvalidFd_HostBacked {
    struct linux_stat st;
    int ret = host_fstat_impl(-1, &st);

    XCTAssertEqual(ret, -EBADF, @"host_fstat_impl should return Linux -EBADF for invalid fd");
}

- (void)testHostFstatReturnsLinuxEfaultForNullStatbuf_HostBacked {
    char host_path[MAX_PATH];
    int fd;
    int ret;

    XCTAssertEqual(vfs_translate_path("/etc/passwd", host_path, sizeof(host_path)), 0,
                   @"vfs_translate_path for /etc/passwd should succeed");

    fd = vfs_test_open_host_path(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host open /etc/passwd should succeed");
    if (fd < 0) {
        return;
    }

    ret = host_fstat_impl(fd, NULL);
    XCTAssertEqual(ret, -EFAULT, @"host_fstat_impl should return Linux -EFAULT for NULL stat buffer");

    host_close_impl(fd);
}

- (void)testHostFstatTranslatesHostStatForValidFd_HostBacked {
    char host_path[MAX_PATH];
    struct linux_stat st;
    int fd;
    int ret;

    XCTAssertEqual(vfs_translate_path("/etc/passwd", host_path, sizeof(host_path)), 0,
                   @"vfs_translate_path for /etc/passwd should succeed");

    fd = vfs_test_open_host_path(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host open /etc/passwd should succeed");
    if (fd < 0) {
        return;
    }

    ret = host_fstat_impl(fd, &st);
    XCTAssertEqual(ret, 0, @"host_fstat_impl should succeed for valid host fd");
    XCTAssertTrue(st.st_mode != 0, @"host_fstat_impl should populate mode");
    XCTAssertTrue(st.st_nlink > 0, @"host_fstat_impl should populate link count");

    host_close_impl(fd);
}

- (void)testVfsTranslatePathAtRelativePathUsesDirfd_HostBacked {
    int real_fd;
    int dirfd;
    char host_dir[MAX_PATH];
    char host_path[MAX_PATH];
    NSString *expected;

    XCTAssertEqual(vfs_translate_path("/tmp/translate-dirfd", host_dir, sizeof(host_dir)), 0,
                   @"dirfd directory should translate");
    vfs_test_ensure_virtual_parent_directory("/tmp/translate-dirfd/file");

    real_fd = vfs_test_open_host_directory_fd(host_dir);
    XCTAssertTrue(real_fd >= 0, @"host directory open should succeed");
    if (real_fd < 0) return;

    dirfd = alloc_fd_impl();
    XCTAssertTrue(dirfd >= 0, @"dirfd allocation should succeed");
    if (dirfd < 0) {
        host_close_impl(real_fd);
        return;
    }

    init_host_dirfd_entry_impl(dirfd, real_fd, 0755, "/tmp/translate-dirfd");

    XCTAssertEqual(vfs_translate_path_at(dirfd, "file", host_path, sizeof(host_path)), 0,
                   @"relative path should resolve from dirfd");
    expected = [NSString stringWithFormat:@"%s/file", host_dir];
    XCTAssertEqualObjects([NSString stringWithUTF8String:host_path], expected,
                          @"relative path should translate from dirfd base directory");

    free_fd_impl(dirfd);
    vfs_test_remove_linux_path("/tmp/translate-dirfd/file");
}

- (void)testSyntheticGetdentsUsesTemporaryUnsupportedPolicy_HostBacked {
    char host_dir[MAX_PATH];
    int real_fd;
    int dirfd;
    char buffer[256];

    XCTAssertEqual(vfs_translate_path("/tmp/synthetic-dirfd-anchor", host_dir, sizeof(host_dir)), 0,
                   @"anchor directory should translate");
    vfs_test_ensure_virtual_parent_directory("/tmp/synthetic-dirfd-anchor/file");

    real_fd = vfs_test_open_host_directory_fd(host_dir);
    XCTAssertTrue(real_fd >= 0, @"host anchor directory open should succeed");
    if (real_fd < 0) return;

    dirfd = alloc_fd_impl();
    XCTAssertTrue(dirfd >= 0, @"synthetic dirfd allocation should succeed");
    if (dirfd < 0) {
        close(real_fd);
        return;
    }

    init_synthetic_subdir_fd_entry_impl(dirfd, O_RDONLY | O_DIRECTORY, 0755, "/proc", SYNTHETIC_DIR_GENERIC);

    errno = 0;
    XCTAssertEqual(getdents64(dirfd, buffer, sizeof(buffer)), -1,
                   @"getdents64 should reject synthetic directories (not yet implemented)");
    XCTAssertEqual(errno, ENOTSUP, @"getdents64 should set ENOTSUP for synthetic directories");

    free_fd_impl(dirfd);
    vfs_test_remove_linux_path("/tmp/synthetic-dirfd-anchor/file");
}

- (void)testSyntheticGetdentsUsesIntentionalUnsupportedPolicy_HostBacked {
    char host_dir[MAX_PATH];
    int real_fd;
    int dirfd;
    char buffer[256];

    XCTAssertEqual(vfs_translate_path("/tmp/getdents-anchor", host_dir, sizeof(host_dir)), 0,
                   @"anchor directory should translate");
    vfs_test_ensure_virtual_directory_exists("/tmp/getdents-anchor");
    vfs_test_seed_linux_file("/tmp/getdents-anchor/file");

    real_fd = vfs_test_open_host_directory_fd(host_dir);
    XCTAssertTrue(real_fd >= 0, @"host anchor directory open should succeed");
    if (real_fd < 0) return;

    dirfd = alloc_fd_impl();
    XCTAssertTrue(dirfd >= 0, @"synthetic dirfd allocation should succeed");
    if (dirfd < 0) {
        close(real_fd);
        return;
    }

    init_synthetic_subdir_fd_entry_impl(dirfd, O_RDONLY | O_DIRECTORY, 0755, "/proc", SYNTHETIC_DIR_GENERIC);

    errno = 0;
    XCTAssertEqual(getdents64(dirfd, buffer, sizeof(buffer)), -1,
                   @"getdents64 should reject synthetic directories (not yet implemented)");
    XCTAssertEqual(errno, ENOTSUP, @"getdents64 should set ENOTSUP for synthetic directories");

    free_fd_impl(dirfd);
    vfs_test_remove_linux_path("/tmp/getdents-anchor/file");
}

- (void)testRenameAllowsSameRoutePersistentMove_HostBacked {
    vfs_test_seed_linux_file("/etc/rename-src");

    int ret = rename("/etc/rename-src", "/etc/rename-dst");
    XCTAssertEqual(ret, 0, @"rename within persistent route should succeed");

    struct linux_stat st;
    XCTAssertEqual(stat_impl("/etc/rename-dst", &st), 0, @"rename destination should exist");
    errno = 0;
    XCTAssertEqual(stat_impl("/etc/rename-src", &st), -1, @"rename source should be gone");
    XCTAssertEqual(errno, ENOENT, @"rename source stat should report ENOENT after move");

    vfs_test_remove_linux_path("/etc/rename-dst");
}

- (void)testRenameatAllowsSameRoutePersistentMove_HostBacked {
    vfs_test_seed_linux_file("/etc/renameat-src");

    int ret = renameat(AT_FDCWD, "/etc/renameat-src", AT_FDCWD, "/etc/renameat-dst");
    XCTAssertEqual(ret, 0, @"renameat within persistent route should succeed");

    struct linux_stat st;
    XCTAssertEqual(stat_impl("/etc/renameat-dst", &st), 0, @"renameat destination should exist");
    errno = 0;
    XCTAssertEqual(stat_impl("/etc/renameat-src", &st), -1, @"renameat source should be gone");
    XCTAssertEqual(errno, ENOENT, @"renameat source stat should report ENOENT after move");

    vfs_test_remove_linux_path("/etc/renameat-dst");
}

- (void)testRenameCrossRouteFails_HostBacked {
    vfs_test_seed_linux_file("/etc/cross-src");

    errno = 0;
    int ret = rename("/etc/cross-src", "/tmp/cross-dst");
    XCTAssertEqual(ret, -1, @"rename across routes should fail");
    XCTAssertEqual(errno, EXDEV, @"rename across routes should fail with EXDEV");

    vfs_test_remove_linux_path("/etc/cross-src");
}

- (void)testRenameatCrossRouteFails_HostBacked {
    vfs_test_seed_linux_file("/etc/crossat-src");

    errno = 0;
    int ret = renameat(AT_FDCWD, "/etc/crossat-src", AT_FDCWD, "/tmp/crossat-dst");
    XCTAssertEqual(ret, -1, @"renameat across routes should fail");
    XCTAssertEqual(errno, EXDEV, @"renameat across routes should fail with EXDEV");

    vfs_test_remove_linux_path("/etc/crossat-src");
}

- (void)testRenameat2CrossRouteFails_HostBacked {
    vfs_test_seed_linux_file("/etc/rn2-cross-src");

    errno = 0;
    int ret = renameat2(AT_FDCWD, "/etc/rn2-cross-src", AT_FDCWD, "/tmp/rn2-cross-dst", 0);
    XCTAssertEqual(ret, -1, @"renameat2 across routes should fail");
    XCTAssertEqual(errno, EXDEV, @"renameat2 across routes should fail with EXDEV");

    vfs_test_remove_linux_path("/etc/rn2-cross-src");
}

- (void)testRenameat2UnknownFlagFails_HostBacked {
    errno = 0;
    int ret = renameat2(AT_FDCWD, "/etc/src", AT_FDCWD, "/etc/dst", INVALID_FLAG_TEST_VALUE);
    XCTAssertEqual(ret, -1, @"renameat2 with unknown flag should fail");
    XCTAssertEqual(errno, EINVAL, @"renameat2 with unknown flag should report EINVAL");
}

- (void)testRenameat2EmptyPathRequiresAtEmptyPathFlag_HostBacked {
    errno = 0;
    int ret = renameat2(AT_FDCWD, "/etc/empty-src", AT_FDCWD, "/etc/empty-dst", 0);
    XCTAssertEqual(ret, -1, @"renameat2 without RENAME_EMPTY_PATH should fail for missing source");
    XCTAssertEqual(errno, ENOENT, @"renameat2 without RENAME_EMPTY_PATH should report ENOENT for missing source");
}

- (void)testFcntlDupFdSucceeds_HostBacked {
    int fd = open("/etc/passwd", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open should succeed");
    if (fd < 0) return;

    int new_fd = fcntl(fd, F_DUPFD, 10);
    XCTAssertTrue(new_fd >= 10, @"F_DUPFD should return fd >= 10");

    close(fd);
    if (new_fd >= 0) close(new_fd);
}

- (void)testFcntlGetFdSucceeds_HostBacked {
    int fd = open("/etc/passwd", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open should succeed");
    if (fd < 0) return;

    int flags = fcntl(fd, F_GETFD);
    XCTAssertTrue(flags >= 0, @"F_GETFD should succeed");

    close(fd);
}

- (void)testFcntlSetFdCloexecSucceeds_HostBacked {
    int fd = open("/etc/passwd", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open should succeed");
    if (fd < 0) return;

    int ret = fcntl(fd, F_SETFD, FD_CLOEXEC);
    XCTAssertEqual(ret, 0, @"F_SETFD with FD_CLOEXEC should succeed");

    int flags = fcntl(fd, F_GETFD);
    XCTAssertTrue((flags & FD_CLOEXEC) != 0, @"FD_CLOEXEC should be set");

    close(fd);
}

- (void)testFcntlGetFlSucceeds_HostBacked {
    int fd = open("/etc/passwd", O_RDONLY);
    XCTAssertTrue(fd >= 0, @"open should succeed");
    if (fd < 0) return;

    int flags = fcntl(fd, F_GETFL);
    XCTAssertTrue(flags >= 0, @"F_GETFL should succeed");
    XCTAssertTrue((flags & O_ACCMODE) == O_RDONLY, @"flags should include O_RDONLY access mode");

    close(fd);
}

- (void)testFcntlDupFdCloexecSucceeds_HostBacked {
    char host_path[MAX_PATH];
    XCTAssertEqual(vfs_translate_path("/etc/passwd", host_path, sizeof(host_path)), 0,
                   @"path should translate");

    int fd = vfs_test_open_host_path(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host open should succeed");
    if (fd < 0) return;

    errno = 0;
    int new_fd = host_test_fcntl_dupfd_cloexec(fd, 10);
    XCTAssertTrue(new_fd >= 0, @"host dupfd_cloexec should succeed");
    if (new_fd < 0) {
        host_close_impl(fd);
        return;
    }

    int flags = host_test_fcntl_getfd(new_fd);
    XCTAssertTrue((flags & FD_CLOEXEC) != 0, @"duped fd should have FD_CLOEXEC");

    host_close_impl(fd);
    host_close_impl(new_fd);
}


- (void)testPersistentFileCreationAndReadWrite_HostBacked {
    const char *test_path = "/etc/test_persistent_file";
    char host_path[MAX_PATH];
    int fd;

    XCTAssertEqual(vfs_translate_path(test_path, host_path, sizeof(host_path)), 0,
                   @"path should translate");

    fd = vfs_test_open_host_path(host_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    XCTAssertTrue(fd >= 0, @"file creation should succeed");
    if (fd < 0) return;

    const char *write_data = "test data";
    ssize_t written = host_write_impl(fd, write_data, strlen(write_data));
    XCTAssertEqual(written, (ssize_t)strlen(write_data), @"write should succeed");

    off_t pos = host_lseek_impl(fd, 0, SEEK_SET);
    XCTAssertEqual(pos, 0, @"seek should succeed");

    char read_buf[64] = {0};
    ssize_t nread = host_read_impl(fd, read_buf, sizeof(read_buf));
    XCTAssertEqual(nread, (ssize_t)strlen(write_data), @"read should return written amount");
    XCTAssertEqual(memcmp(read_buf, write_data, strlen(write_data)), 0,
                    @"read data should match written data");

    host_close_impl(fd);
    host_unlink_impl(host_path);
}

- (void)testPersistentDirectoryCreation_HostBacked {
    const char *test_dir = "/etc/test_persistent_dir";
    char host_path[MAX_PATH];

    XCTAssertEqual(vfs_translate_path(test_dir, host_path, sizeof(host_path)), 0,
                   @"directory path should translate");
    vfs_test_ensure_virtual_parent_directory(test_dir);

    int ret = host_mkdir_impl(host_path, 0755);
    XCTAssertTrue(ret == 0 || errno == EEXIST, @"directory creation should succeed");

    struct linux_stat st;
    XCTAssertEqual(stat_impl(test_dir, &st), 0, @"stat should succeed for created directory");
    XCTAssertTrue((st.st_mode & S_IFMT) == S_IFDIR, @"created path should be a directory");

    host_rmdir_impl(host_path);
}

- (void)testPersistentSymbolicLink_HostBacked {
    const char *link_path = "/etc/test_symlink";
    const char *target = "/etc/passwd";
    char host_link[MAX_PATH];

    XCTAssertEqual(vfs_translate_path(link_path, host_link, sizeof(host_link)), 0,
                   @"link path should translate");
    vfs_test_ensure_virtual_parent_directory(link_path);

    host_unlink_impl(host_link);

    int ret = host_symlink_impl(target, host_link);
    XCTAssertEqual(ret, 0, @"symlink creation should succeed");

    struct linux_stat st;
    XCTAssertEqual(lstat_impl(link_path, &st), 0, @"lstat should succeed for symlink");
    XCTAssertTrue((st.st_mode & S_IFMT) == S_IFLNK, @"created path should be a symlink");

    host_unlink_impl(host_link);
}

@end
