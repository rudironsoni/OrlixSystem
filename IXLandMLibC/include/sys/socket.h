#ifndef IXLAND_MLIBC_SYS_SOCKET_H
#define IXLAND_MLIBC_SYS_SOCKET_H

#ifndef _SYS_SOCKET_H_
#define _SYS_SOCKET_H_
#endif

#include "types.h"
#include "uio.h"
#include "../time.h"

typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __ss_padding[118];
    unsigned long __ss_align;
};

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif
#ifndef AF_NETLINK
#define AF_NETLINK 16
#endif
#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef AF_MAX
#define AF_MAX 46
#endif

#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif
#ifndef SOCK_RDM
#define SOCK_RDM 4
#endif
#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif
#ifndef SOCK_DCCP
#define SOCK_DCCP 6
#endif
#ifndef SOCK_PACKET
#define SOCK_PACKET 10
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 00004000
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

#ifndef SOL_SOCKET
#define SOL_SOCKET 1
#endif
#ifndef SO_REUSEADDR
#define SO_REUSEADDR 2
#endif
#ifndef SO_TYPE
#define SO_TYPE 3
#endif
#ifndef SO_ERROR
#define SO_ERROR 4
#endif
#ifndef SO_SNDBUF
#define SO_SNDBUF 7
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF 8
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 9
#endif
#ifndef SO_ACCEPTCONN
#define SO_ACCEPTCONN 30
#endif
#ifndef SO_PROTOCOL
#define SO_PROTOCOL 38
#endif
#ifndef SO_DOMAIN
#define SO_DOMAIN 39
#endif

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
#ifndef SHUT_RD
#define SHUT_RD 0
#endif
#ifndef SHUT_WR
#define SHUT_WR 1
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

#endif
