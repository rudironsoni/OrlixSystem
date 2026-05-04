/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Generated Linux ABI supplement for IXLandSystem.
 *
 * Source: Linux v6.12 adc218676eef25575469234709c2d87185ca223a
 * Source file: include/linux/fs.h
 * Generator: scripts/generate_linux_abi_supplement.sh
 *
 * Do not edit by hand. Regenerate from the matching Linux source tree.
 */
#ifndef _LINUX_UMOUNT_H
#define _LINUX_UMOUNT_H

#define MNT_FORCE	0x00000001	/* Attempt to forcibily umount */
#define MNT_DETACH	0x00000002	/* Just detach from the tree */
#define MNT_EXPIRE	0x00000004	/* Mark for expiry */
#define UMOUNT_NOFOLLOW	0x00000008	/* Don't follow symlink on umount */
#define UMOUNT_UNUSED	0x80000000	/* Flag guaranteed to be unused */

#endif /* _LINUX_UMOUNT_H */
