#ifndef IXLAND_MLIBC_BITS_ALLTYPES_H
#define IXLAND_MLIBC_BITS_ALLTYPES_H

#ifndef _SSIZE_T
#define _SSIZE_T
typedef __INTPTR_TYPE__ ssize_t;
#endif

#ifndef _TIME_T
#define _TIME_T
typedef __INTPTR_TYPE__ time_t;
#endif

#ifndef _PID_T
#define _PID_T
typedef __INT32_TYPE__ pid_t;
#endif

#ifndef _UID_T
#define _UID_T
typedef __UINT32_TYPE__ uid_t;
#endif

#ifndef _GID_T
#define _GID_T
typedef __UINT32_TYPE__ gid_t;
#endif

#ifndef _MODE_T
#define _MODE_T
typedef __UINT32_TYPE__ mode_t;
#endif

#ifndef _INO_T
#define _INO_T
typedef __UINT64_TYPE__ ino_t;
#endif

#ifndef _OFF_T
#define _OFF_T
typedef __INT64_TYPE__ off_t;
#endif

#ifndef _SUSECONDS_T
#define _SUSECONDS_T
typedef __INT32_TYPE__ suseconds_t;
#endif

#ifndef _DEV_T
#define _DEV_T
typedef __UINT64_TYPE__ dev_t;
#endif

#ifndef _NLINK_T
#define _NLINK_T
typedef __UINT32_TYPE__ nlink_t;
#endif

#ifndef _BLKSIZE_T
#define _BLKSIZE_T
typedef __INTPTR_TYPE__ blksize_t;
#endif

#ifndef _BLKCNT_T
#define _BLKCNT_T
typedef __INT64_TYPE__ blkcnt_t;
#endif

#ifndef _SOCKLEN_T
#define _SOCKLEN_T
typedef __UINT32_TYPE__ socklen_t;
#endif

#ifndef _USECONDS_T
#define _USECONDS_T
typedef __UINT32_TYPE__ useconds_t;
#endif

#endif
