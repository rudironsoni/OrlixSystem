#ifndef IXLAND_HOST_ADAPTER_FS_IOCTL_HOST_H
#define IXLAND_HOST_ADAPTER_FS_IOCTL_HOST_H

int host_ioctl_impl(int fd, unsigned long request, void *arg);

#endif
