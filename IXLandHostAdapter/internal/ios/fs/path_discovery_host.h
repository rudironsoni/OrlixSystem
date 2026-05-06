/* internal/ios/fs/path_discovery_host.h
 * Narrow seam for host container path discovery
 *
 * This header provides ONLY function declarations.
 * Implementation is in backing_paths.m (bridge layer).
 */

#ifndef PATH_DISCOVERY_HOST_H
#define PATH_DISCOVERY_HOST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host container path discovery - quarantined in iOS bridge */
int vfs_discover_persistent_root(char *path, size_t path_len);
int vfs_discover_cache_root(char *path, size_t path_len);
int vfs_discover_temp_root(char *path, size_t path_len);

/* Host directory creation */
int host_ensure_directory_impl(const char *path, uint32_t mode);

#ifdef __cplusplus
}
#endif

#endif /* PATH_DISCOVERY_HOST_H */
