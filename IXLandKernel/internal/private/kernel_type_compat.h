#ifndef IXLAND_INTERNAL_PRIVATE_KERNEL_TYPE_COMPAT_H
#define IXLAND_INTERNAL_PRIVATE_KERNEL_TYPE_COMPAT_H

/*
 * Kernel-private compatibility shims for libc-spelled scalar types used by
 * internal Linux-owner code. These are not package-facing headers and must not
 * be exported as sysroot truth.
 */

#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef __INTPTR_TYPE__ ssize_t;
#endif

#ifndef __pid_t_defined
#define __pid_t_defined
typedef __INT32_TYPE__ pid_t;
#endif

#ifndef __uid_t_defined
#define __uid_t_defined
typedef __UINT32_TYPE__ uid_t;
#endif

#ifndef __gid_t_defined
#define __gid_t_defined
typedef __UINT32_TYPE__ gid_t;
#endif

#ifndef __off_t_defined
#define __off_t_defined
typedef __INT64_TYPE__ off_t;
#endif

#ifndef __nfds_t_defined
#define __nfds_t_defined
typedef __UINT32_TYPE__ nfds_t;
#endif

#endif
