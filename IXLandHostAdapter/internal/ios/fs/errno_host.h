#ifndef IXLAND_INTERNAL_IOS_FS_ERRNO_HOST_H
#define IXLAND_INTERNAL_IOS_FS_ERRNO_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

int host_errno_to_linux_errno(int host_errno);

#ifdef __cplusplus
}
#endif

#endif
