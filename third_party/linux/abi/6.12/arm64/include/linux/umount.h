/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UMOUNT_H
#define _LINUX_UMOUNT_H

#define MNT_FORCE	0x00000001	/* Attempt to forcibily umount */
#define MNT_DETACH	0x00000002	/* Just detach from the tree */
#define MNT_EXPIRE	0x00000004	/* Mark for expiry */
#define UMOUNT_NOFOLLOW	0x00000008	/* Don't follow symlink on umount */
#define UMOUNT_UNUSED	0x80000000	/* Flag guaranteed to be unused */

#endif
