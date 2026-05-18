#include "backing_stat_translate.h"

#include <asm/stat.h>
#include <string.h>

void backing_stat_translate(const struct backing_stat_data *source, struct stat *target) {
    if (!source || !target) {
        return;
    }

    memset(target, 0, sizeof(*target));
    target->st_dev = (unsigned long)source->dev;
    target->st_ino = (unsigned long)source->ino;
    target->st_mode = source->mode;
    target->st_nlink = source->nlink;
    target->st_uid = source->uid;
    target->st_gid = source->gid;
    target->st_rdev = (unsigned long)source->rdev;
    target->st_size = (long)source->size;
    target->st_blksize = source->blksize;
    target->st_blocks = (long)source->blocks;
    target->st_atime = (long)source->atime_sec;
    target->st_atime_nsec = (unsigned long)source->atime_nsec;
    target->st_mtime = (long)source->mtime_sec;
    target->st_mtime_nsec = (unsigned long)source->mtime_nsec;
    target->st_ctime = (long)source->ctime_sec;
    target->st_ctime_nsec = (unsigned long)source->ctime_nsec;
}
