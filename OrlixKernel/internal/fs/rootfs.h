#ifndef INTERNAL_FS_ROOTFS_H
#define INTERNAL_FS_ROOTFS_H

#include <linux/stddef.h>

int backing_root_discover_persistent(char *path, size_t path_len);
int backing_root_discover_cache(char *path, size_t path_len);
int backing_root_discover_temp(char *path, size_t path_len);

#endif
