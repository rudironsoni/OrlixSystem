#include "SyscallUioContract.h"

#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/uio.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime/syscall.h"

extern int close_impl(int fd);
extern int unlink_impl(const char *pathname);
extern ssize_t pread_impl(int fd, void *buf, size_t count, long long offset);
extern int pipe_impl(int pipefd[2]);

static int close_if_open(int fd) {
    if (fd >= 0) {
        return close_impl(fd);
    }
    return 0;
}

int syscall_uio_contract_readv_writev_round_trip(void) {
    const char *path = "/tmp/syscall-uio-round-trip";
    char left[] = "shell";
    char right[] = "-uio";
    char out_left[6];
    char out_right[5];
    struct iovec write_iov[2];
    struct iovec read_iov[2];
    int fd = -1;
    long ret;

    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }

    write_iov[0].iov_base = left;
    write_iov[0].iov_len = 5;
    write_iov[1].iov_base = right;
    write_iov[1].iov_len = 4;
    ret = syscall_dispatch_impl(__NR_writev, fd, (long)(uintptr_t)write_iov, 2, 0, 0, 0);
    if (ret != 9) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    close_if_open(fd);

    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_RDONLY, 0, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    memset(out_left, 0, sizeof(out_left));
    memset(out_right, 0, sizeof(out_right));
    read_iov[0].iov_base = out_left;
    read_iov[0].iov_len = 5;
    read_iov[1].iov_base = out_right;
    read_iov[1].iov_len = 4;
    ret = syscall_dispatch_impl(__NR_readv, fd, (long)(uintptr_t)read_iov, 2, 0, 0, 0);
    if (ret != 9 || strcmp(out_left, "shell") != 0 || strcmp(out_right, "-uio") != 0) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }

    close_if_open(fd);
    fd = -1;
    unlink_impl(path);
    return 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    return -1;
}

int syscall_uio_contract_rejects_invalid_iov_count(void) {
    long ret = syscall_dispatch_impl(__NR_readv, 0, 0, UIO_MAXIOV + 1, 0, 0, 0);

    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    return 0;
}

int syscall_uio_contract_truncate_changes_file_size_by_path(void) {
    const char *path = "/tmp/syscall-uio-truncate";
    const char *missing_path = "/tmp/syscall-uio-truncate-missing";
    char page[8] = "abcdefg";
    char probe = '\0';
    int fd = -1;
    long ret;

    unlink_impl(path);
    unlink_impl(missing_path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, 7, 0, 0, 0);
    if (ret != 7) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_truncate, (long)(uintptr_t)path, 3, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (pread_impl(fd, &probe, 1, 2) != 1 || probe != 'c' ||
        pread_impl(fd, &probe, 1, 3) != 0) {
        errno = EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_truncate, (long)(uintptr_t)path, 6, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (pread_impl(fd, &probe, 1, 5) != 1 || probe != '\0') {
        errno = EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_truncate, (long)(uintptr_t)missing_path, 1, 0, 0, 0, 0);
    if (ret != -ENOENT) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(fd);
    fd = -1;
    unlink_impl(path);
    unlink_impl(missing_path);
    return 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    unlink_impl(missing_path);
    return -1;
}

int syscall_uio_contract_preadv_pwritev_preserve_file_offset(void) {
    const char *path = "/tmp/syscall-uio-preadv";
    char seed[] = "0123456789";
    char left[3];
    char right[3];
    struct iovec write_iov[2];
    struct iovec read_iov[2];
    int fd = -1;
    long ret;

    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)seed, 10, 0, 0, 0);
    if (ret != 10) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_lseek, fd, 1, SEEK_SET, 0, 0, 0) != 1) {
        errno = EIO;
        goto out;
    }

    write_iov[0].iov_base = (void *)"ab";
    write_iov[0].iov_len = 2;
    write_iov[1].iov_base = (void *)"cd";
    write_iov[1].iov_len = 2;
    ret = syscall_dispatch_impl(__NR_pwritev, fd, (long)(uintptr_t)write_iov, 2, 4, 0, 0);
    if (ret != 4 || syscall_dispatch_impl(__NR_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    read_iov[0].iov_base = left;
    read_iov[0].iov_len = 2;
    read_iov[1].iov_base = right;
    read_iov[1].iov_len = 2;
    ret = syscall_dispatch_impl(__NR_preadv, fd, (long)(uintptr_t)read_iov, 2, 4, 0, 0);
    if (ret != 4 || strcmp(left, "ab") != 0 || strcmp(right, "cd") != 0 ||
        syscall_dispatch_impl(__NR_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(fd);
    fd = -1;
    unlink_impl(path);
    return 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    return -1;
}

int syscall_uio_contract_preadv2_pwritev2_flag_policy(void) {
    const char *path = "/tmp/syscall-uio-preadv2";
    char out[5];
    struct iovec iov;
    int fd = -1;
    long ret;

    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }

    iov.iov_base = (void *)"wxyz";
    iov.iov_len = 4;
    ret = syscall_dispatch_impl(__NR_pwritev2, fd, (long)(uintptr_t)&iov, 1, 0, 0, 0);
    if (ret != 4) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }

    memset(out, 0, sizeof(out));
    iov.iov_base = out;
    iov.iov_len = 4;
    ret = syscall_dispatch_impl(__NR_preadv2, fd, (long)(uintptr_t)&iov, 1, 0, 0, 0);
    if (ret != 4 || strcmp(out, "wxyz") != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pwritev2, fd, (long)(uintptr_t)&iov, 1, 0, 0, 1);
    if (ret != -EOPNOTSUPP) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_preadv2, fd, (long)(uintptr_t)&iov, 1, 0, 0, 1);
    if (ret != -EOPNOTSUPP) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(fd);
    fd = -1;
    unlink_impl(path);
    return 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    return -1;
}

int syscall_uio_contract_sendfile_honors_offset_rules(void) {
    const char *src = "/tmp/syscall-uio-sendfile-src";
    const char *dst = "/tmp/syscall-uio-sendfile-dst";
    char input[] = "transfer";
    char out[16];
    long long offset = 1;
    int src_fd = -1;
    int dst_fd = -1;
    long ret;

    unlink_impl(src);
    unlink_impl(dst);
    src_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)src,
                                        O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (src_fd < 0) {
        errno = (int)-src_fd;
        goto out;
    }
    dst_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)dst,
                                        O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (dst_fd < 0) {
        errno = (int)-dst_fd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_write, src_fd, (long)(uintptr_t)input, 8, 0, 0, 0);
    if (ret != 8) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_lseek, src_fd, 0, SEEK_SET, 0, 0, 0) != 0) {
        errno = EIO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_sendfile, dst_fd, src_fd, (long)(uintptr_t)&offset, 4, 0, 0);
    if (ret != 4 || offset != 5 || syscall_dispatch_impl(__NR_lseek, src_fd, 0, SEEK_CUR, 0, 0, 0) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(out, 0, sizeof(out));
    if (pread_impl(dst_fd, out, 4, 0) != 4 || memcmp(out, "rans", 4) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (syscall_dispatch_impl(__NR_lseek, src_fd, 0, SEEK_SET, 0, 0, 0) != 0) {
        errno = EIO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_sendfile, dst_fd, src_fd, 0, 2, 0, 0);
    if (ret != 2 || syscall_dispatch_impl(__NR_lseek, src_fd, 0, SEEK_CUR, 0, 0, 0) != 2) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(src_fd);
    close_if_open(dst_fd);
    unlink_impl(src);
    unlink_impl(dst);
    return 0;

out:
    close_if_open(src_fd);
    close_if_open(dst_fd);
    unlink_impl(src);
    unlink_impl(dst);
    return -1;
}

int syscall_uio_contract_fallocate_extends_file_and_zero_fills(void) {
    const char *path = "/tmp/syscall-uio-fallocate";
    char byte = '\0';
    int fd = -1;
    long ret;

    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_fallocate, fd, 0, 2, 4, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_lseek, fd, 0, SEEK_END, 0, 0, 0) != 6) {
        errno = EPROTO;
        goto out;
    }
    if (pread_impl(fd, &byte, 1, 5) != 1 || byte != '\0') {
        errno = EPROTO;
        goto out;
    }

    close_if_open(fd);
    fd = -1;
    unlink_impl(path);
    return 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    return -1;
}

int syscall_uio_contract_sync_file_range_accepts_zero_flags(void) {
    const char *path = "/tmp/syscall-uio-sync-range";
    int fd = -1;
    long ret;

    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)"sync", 4, 0, 0, 0);
    if (ret != 4) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_sync_file_range, fd, 0, 4, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(fd);
    fd = -1;
    unlink_impl(path);
    return 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    return -1;
}

int syscall_uio_contract_splice_moves_bytes_between_file_and_pipe(void) {
    const char *src = "/tmp/syscall-uio-splice-src";
    const char *dst = "/tmp/syscall-uio-splice-dst";
    char out[8];
    long long offset = 1;
    int src_fd = -1;
    int dst_fd = -1;
    int pipefd[2] = {-1, -1};
    long ret;

    unlink_impl(src);
    unlink_impl(dst);
    src_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)src,
                                        O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (src_fd < 0) {
        errno = (int)-src_fd;
        goto out;
    }
    dst_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)dst,
                                        O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (dst_fd < 0 || pipe_impl(pipefd) != 0) {
        errno = errno ? errno : EIO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_write, src_fd, (long)(uintptr_t)"splice", 6, 0, 0, 0);
    if (ret != 6) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_lseek, src_fd, 0, SEEK_SET, 0, 0, 0) != 0) {
        errno = EIO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_splice, src_fd, (long)(uintptr_t)&offset,
                                pipefd[1], 0, 4, 0);
    if (ret != 4) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (offset != 5) {
        errno = EPROTO;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_lseek, src_fd, 0, SEEK_CUR, 0, 0, 0) != 0) {
        errno = EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_splice, pipefd[0], 0, dst_fd, 0, 4, 0);
    if (ret != 4) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(out, 0, sizeof(out));
    if (pread_impl(dst_fd, out, 4, 0) != 4 || memcmp(out, "plic", 4) != 0) {
        errno = EPROTO;
        goto out;
    }

    close_if_open(src_fd);
    close_if_open(dst_fd);
    close_if_open(pipefd[0]);
    close_if_open(pipefd[1]);
    unlink_impl(src);
    unlink_impl(dst);
    return 0;

out:
    close_if_open(src_fd);
    close_if_open(dst_fd);
    close_if_open(pipefd[0]);
    close_if_open(pipefd[1]);
    unlink_impl(src);
    unlink_impl(dst);
    return -1;
}

int syscall_uio_contract_vmsplice_and_tee_preserve_pipe_payloads(void) {
    char left[8];
    char right[8];
    struct iovec iov;
    int src[2] = {-1, -1};
    int dst[2] = {-1, -1};
    long ret;

    if (pipe_impl(src) != 0 || pipe_impl(dst) != 0) {
        errno = errno ? errno : EIO;
        goto out;
    }

    iov.iov_base = (void *)"abc";
    iov.iov_len = 3;
    ret = syscall_dispatch_impl(__NR_vmsplice, src[1], (long)(uintptr_t)&iov, 1, 0, 0, 0);
    if (ret != 3) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_tee, src[0], dst[1], 3, 0, 0, 0);
    if (ret != 3) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    if (syscall_dispatch_impl(__NR_read, src[0], (long)(uintptr_t)left, 3, 0, 0, 0) != 3 ||
        syscall_dispatch_impl(__NR_read, dst[0], (long)(uintptr_t)right, 3, 0, 0, 0) != 3 ||
        memcmp(left, "abc", 3) != 0 || memcmp(right, "abc", 3) != 0) {
        errno = EPROTO;
        goto out;
    }

    close_if_open(src[0]);
    close_if_open(src[1]);
    close_if_open(dst[0]);
    close_if_open(dst[1]);
    return 0;

out:
    close_if_open(src[0]);
    close_if_open(src[1]);
    close_if_open(dst[0]);
    close_if_open(dst[1]);
    return -1;
}
