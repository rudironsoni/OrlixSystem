#ifndef IXLAND_INTERNAL_PRIVATE_BACKING_ROOTS_H
#define IXLAND_INTERNAL_PRIVATE_BACKING_ROOTS_H

#include <stddef.h>

int backing_root_discover_persistent(char *path, size_t path_len);
int backing_root_discover_cache(char *path, size_t path_len);
int backing_root_discover_temp(char *path, size_t path_len);

#endif
