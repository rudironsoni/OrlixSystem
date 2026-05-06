/* internal/ios/fs/ioctl_host.h
 * Narrow seam for ioctl host operations
 */

#ifndef IOCTL_HOST_H
#define IOCTL_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Host ioctl passthrough - for fds that need host backing */
int host_ioctl_impl(int fd, unsigned long request, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* IOCTL_HOST_H */
