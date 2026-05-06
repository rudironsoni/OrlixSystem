/* fs/stat_types.h
 * Linux-compatible stat structure definition.
 */

#ifndef FS_STAT_TYPES_H
#define FS_STAT_TYPES_H

struct linux_stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned long st_rdev;
    unsigned long __pad1;
    long st_size;
    int st_blksize;
    int __pad2;
    long st_blocks;
    long st_atime_sec;
    unsigned long st_atime_nsec;
    long st_mtime_sec;
    unsigned long st_mtime_nsec;
    long st_ctime_sec;
    unsigned long st_ctime_nsec;
    unsigned int __unused4;
    unsigned int __unused5;
};

#endif /* FS_STAT_TYPES_H */
