#ifndef ORLIX_MACOS_GETHOSTUUID_COMPAT_H
#define ORLIX_MACOS_GETHOSTUUID_COMPAT_H

struct timespec;
int gethostuuid(unsigned char *, const struct timespec *);

#endif
