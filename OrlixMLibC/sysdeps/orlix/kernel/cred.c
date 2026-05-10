#include <errno.h>
#include <stdarg.h>

#include <linux/types.h>
#include <linux/capability.h>
extern __kernel_uid32_t getuid_impl(void);
extern __kernel_uid32_t geteuid_impl(void);
extern __kernel_gid32_t getgid_impl(void);
extern __kernel_gid32_t getegid_impl(void);
extern int setuid_impl(__kernel_uid32_t uid);
extern int setgid_impl(__kernel_gid32_t gid);
extern int seteuid_impl(__kernel_uid32_t euid);
extern int setegid_impl(__kernel_gid32_t egid);
extern int setresuid_impl(__kernel_uid32_t ruid, __kernel_uid32_t euid, __kernel_uid32_t suid);
extern int setresgid_impl(__kernel_gid32_t rgid, __kernel_gid32_t egid, __kernel_gid32_t sgid);
extern int setreuid_impl(__kernel_uid32_t ruid, __kernel_uid32_t euid);
extern int setregid_impl(__kernel_gid32_t rgid, __kernel_gid32_t egid);
extern int getresuid_impl(__kernel_uid32_t *ruid, __kernel_uid32_t *euid, __kernel_uid32_t *suid);
extern int getresgid_impl(__kernel_gid32_t *rgid, __kernel_gid32_t *egid, __kernel_gid32_t *sgid);
extern __kernel_uid32_t setfsuid_impl(__kernel_uid32_t fsuid);
extern __kernel_gid32_t setfsgid_impl(__kernel_gid32_t fsgid);
extern int getgroups_impl(int size, __kernel_gid32_t list[]);
extern int setgroups_impl(int size, const __kernel_gid32_t *list);
extern int prctl_impl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);
extern int capget_impl(cap_user_header_t header, cap_user_data_t data);
extern int capset_impl(cap_user_header_t header, const cap_user_data_t data);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) __kernel_uid32_t getuid(void) {
    return getuid_impl();
}

__attribute__((visibility("default"))) __kernel_uid32_t geteuid(void) {
    return geteuid_impl();
}

__attribute__((visibility("default"))) __kernel_gid32_t getgid(void) {
    return getgid_impl();
}

__attribute__((visibility("default"))) __kernel_gid32_t getegid(void) {
    return getegid_impl();
}

__attribute__((visibility("default"))) int setuid(__kernel_uid32_t uid) {
    return wrap_int_result(setuid_impl(uid));
}

__attribute__((visibility("default"))) int setgid(__kernel_gid32_t gid) {
    return wrap_int_result(setgid_impl(gid));
}

__attribute__((visibility("default"))) int seteuid(__kernel_uid32_t euid) {
    return wrap_int_result(seteuid_impl(euid));
}

__attribute__((visibility("default"))) int setegid(__kernel_gid32_t egid) {
    return wrap_int_result(setegid_impl(egid));
}

__attribute__((visibility("default"))) int setresuid(__kernel_uid32_t ruid,
                                                     __kernel_uid32_t euid,
                                                     __kernel_uid32_t suid) {
    return wrap_int_result(setresuid_impl(ruid, euid, suid));
}

__attribute__((visibility("default"))) int setresgid(__kernel_gid32_t rgid,
                                                     __kernel_gid32_t egid,
                                                     __kernel_gid32_t sgid) {
    return wrap_int_result(setresgid_impl(rgid, egid, sgid));
}

__attribute__((visibility("default"))) int setreuid(__kernel_uid32_t ruid, __kernel_uid32_t euid) {
    return wrap_int_result(setreuid_impl(ruid, euid));
}

__attribute__((visibility("default"))) int setregid(__kernel_gid32_t rgid, __kernel_gid32_t egid) {
    return wrap_int_result(setregid_impl(rgid, egid));
}

__attribute__((visibility("default"))) int getresuid(__kernel_uid32_t *ruid,
                                                     __kernel_uid32_t *euid,
                                                     __kernel_uid32_t *suid) {
    return wrap_int_result(getresuid_impl(ruid, euid, suid));
}

__attribute__((visibility("default"))) int getresgid(__kernel_gid32_t *rgid,
                                                     __kernel_gid32_t *egid,
                                                     __kernel_gid32_t *sgid) {
    return wrap_int_result(getresgid_impl(rgid, egid, sgid));
}

__attribute__((visibility("default"))) __kernel_uid32_t setfsuid(__kernel_uid32_t fsuid) {
    return setfsuid_impl(fsuid);
}

__attribute__((visibility("default"))) __kernel_gid32_t setfsgid(__kernel_gid32_t fsgid) {
    return setfsgid_impl(fsgid);
}

__attribute__((visibility("default"))) int getgroups(int size, __kernel_gid32_t list[]) {
    return wrap_int_result(getgroups_impl(size, list));
}

__attribute__((visibility("default"))) int setgroups(int size, const __kernel_gid32_t *list) {
    return wrap_int_result(setgroups_impl(size, list));
}

__attribute__((visibility("default"))) int prctl(int option, ...) {
    va_list ap;
    unsigned long arg2;
    unsigned long arg3;
    unsigned long arg4;
    unsigned long arg5;

    va_start(ap, option);
    arg2 = va_arg(ap, unsigned long);
    arg3 = va_arg(ap, unsigned long);
    arg4 = va_arg(ap, unsigned long);
    arg5 = va_arg(ap, unsigned long);
    va_end(ap);

    return wrap_int_result(prctl_impl(option, arg2, arg3, arg4, arg5));
}

__attribute__((visibility("default"))) int capget(cap_user_header_t header, cap_user_data_t data) {
    return wrap_int_result(capget_impl(header, data));
}

__attribute__((visibility("default"))) int capset(cap_user_header_t header, const cap_user_data_t data) {
    return wrap_int_result(capset_impl(header, data));
}
