#ifndef ORLIX_LINUX_UMOUNT2_FLAGS_H
#define ORLIX_LINUX_UMOUNT2_FLAGS_H

/*
 * Linux umount2(2) flag values.
 *
 * On Linux 6.12 these values are owned by the full kernel header
 * include/linux/fs.h rather than by installed UAPI. Product code that can
 * consume the full owner should prefer that path directly.
 *
 * This transitional header remains only for mixed-owner kernel tests that
 * still cannot include full linux/fs.h without dragging incompatible repo
 * VFS owner headers into the same translation unit.
 */

#define MNT_FORCE 0x00000001
#define MNT_DETACH 0x00000002
#define MNT_EXPIRE 0x00000004
#define UMOUNT_NOFOLLOW 0x00000008
#define UMOUNT_UNUSED 0x80000000

#endif
