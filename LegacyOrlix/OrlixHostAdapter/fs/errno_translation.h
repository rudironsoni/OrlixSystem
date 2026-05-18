#ifndef ORLIX_HOST_ADAPTER_FS_ERRNO_TRANSLATION_H
#define ORLIX_HOST_ADAPTER_FS_ERRNO_TRANSLATION_H

#ifdef __cplusplus
extern "C" {
#endif

int linux_errno_from_darwin_errno(int darwin_errno);

#ifdef __cplusplus
}
#endif

#endif
