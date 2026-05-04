/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Generated Linux ABI supplement for IXLandSystem.
 *
 * Source: Linux v6.12 adc218676eef25575469234709c2d87185ca223a
 * Source file: include/linux/statfs.h
 * Generator: scripts/generate_linux_abi_supplement.sh
 *
 * Do not edit by hand. Regenerate from the matching Linux source tree.
 */
#ifndef _LINUX_STATFS_H
#define _LINUX_STATFS_H

#define ST_RDONLY	0x0001	/* mount read-only */
#define ST_NOSUID	0x0002	/* ignore suid and sgid bits */
#define ST_NODEV	0x0004	/* disallow access to device special files */
#define ST_NOEXEC	0x0008	/* disallow program execution */
#define ST_SYNCHRONOUS	0x0010	/* writes are synced at once */
#define ST_VALID	0x0020	/* f_flags support is implemented */
#define ST_MANDLOCK	0x0040	/* allow mandatory locks on an FS */
#define ST_NOATIME	0x0400	/* do not update access times */
#define ST_NODIRATIME	0x0800	/* do not update directory access times */
#define ST_RELATIME	0x1000	/* update atime relative to mtime/ctime */
#define ST_NOSYMFOLLOW	0x2000	/* do not follow symlinks */

#endif /* _LINUX_STATFS_H */
