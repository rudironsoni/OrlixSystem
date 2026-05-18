#ifndef ORLIX_HOST_ADAPTER_FS_BACKING_STAT_TRANSLATE_H
#define ORLIX_HOST_ADAPTER_FS_BACKING_STAT_TRANSLATE_H

#include <stdint.h>

struct stat;

struct backing_stat_data {
    uint64_t dev;
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t rdev;
    int64_t size;
    int32_t blksize;
    int64_t blocks;
    int64_t atime_sec;
    uint64_t atime_nsec;
    int64_t mtime_sec;
    uint64_t mtime_nsec;
    int64_t ctime_sec;
    uint64_t ctime_nsec;
};

void backing_stat_translate(const struct backing_stat_data *source, struct stat *target);

#endif
