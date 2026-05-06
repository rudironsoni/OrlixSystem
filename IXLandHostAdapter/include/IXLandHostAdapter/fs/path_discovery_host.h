#ifndef IXLAND_HOST_ADAPTER_FS_PATH_DISCOVERY_HOST_H
#define IXLAND_HOST_ADAPTER_FS_PATH_DISCOVERY_HOST_H

#include <stddef.h>
#include <stdint.h>

int vfs_discover_persistent_root(char *path, size_t path_len);
int vfs_discover_cache_root(char *path, size_t path_len);
int vfs_discover_temp_root(char *path, size_t path_len);
int host_ensure_directory_impl(const char *path, uint32_t mode);

#endif
