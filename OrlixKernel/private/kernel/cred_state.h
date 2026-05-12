#ifndef PRIVATE_KERNEL_CRED_STATE_H
#define PRIVATE_KERNEL_CRED_STATE_H

#include "kernel/cred.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cred {
    __kernel_uid32_t uid;
    __kernel_gid32_t gid;
    __kernel_uid32_t euid;
    __kernel_gid32_t egid;
    __kernel_uid32_t suid;
    __kernel_gid32_t sgid;
    __kernel_uid32_t fsuid;
    __kernel_gid32_t fsgid;
    __kernel_gid32_t *groups;
    size_t group_count;
    bool no_new_privs;
    uint32_t securebits;
    uint64_t cap_permitted;
    uint64_t cap_effective;
    uint64_t cap_inheritable;
    uint64_t cap_bounding;
    uint64_t cap_ambient;
    uint64_t user_ns_id;
    __kernel_uid32_t uid_map_inside;
    __kernel_uid32_t uid_map_outside;
    __kernel_uid32_t uid_map_count;
    __kernel_gid32_t gid_map_inside;
    __kernel_gid32_t gid_map_outside;
    __kernel_gid32_t gid_map_count;
    bool setgroups_allowed;
    int refs;
};

#ifdef __cplusplus
}
#endif

#endif
