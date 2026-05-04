#ifndef IXLAND_LINUX_UMOUNT2_FLAGS_H
#define IXLAND_LINUX_UMOUNT2_FLAGS_H

/*
 * Linux umount2(2) flag values.
 *
 * On Linux, userspace typically gets these from libc headers (e.g. <sys/mount.h>).
 * They are not present in the exported UAPI header set produced by
 * `make headers_install` for Linux 6.12.
 *
 * Values are sourced from upstream Linux 6.12: include/linux/fs.h.
 *
 * NOTE: This header is IXLandSystem-private glue for the virtual-kernel syscall
 * surface and tests. It must not be treated as a vendored Linux header.
 */

#define MNT_FORCE 0x00000001
#define MNT_DETACH 0x00000002
#define MNT_EXPIRE 0x00000004
#define UMOUNT_NOFOLLOW 0x00000008
#define UMOUNT_UNUSED 0x80000000

#endif /* IXLAND_LINUX_UMOUNT2_FLAGS_H */

