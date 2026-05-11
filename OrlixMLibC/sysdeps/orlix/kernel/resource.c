#include <errno.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/times.h>

extern int getrlimit_impl(int resource, struct rlimit *rlim);
extern int setrlimit_impl(int resource, const struct rlimit *rlim);
extern long times_impl(struct tms *buf);
extern int getrusage_impl(int who, struct rusage *usage);
extern int prlimit_impl(int32_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int getrlimit(int resource, struct rlimit *rlim) {
    return wrap_int_result(getrlimit_impl(resource, rlim));
}

__attribute__((visibility("default"))) int setrlimit(int resource, const struct rlimit *rlim) {
    return wrap_int_result(setrlimit_impl(resource, rlim));
}

__attribute__((visibility("default"))) int getrlimit64(int resource, struct rlimit64 *rlim) {
    if (!rlim) {
        errno = EFAULT;
        return -1;
    }
    return wrap_int_result(getrlimit_impl(resource, (struct rlimit *)rlim));
}

__attribute__((visibility("default"))) int setrlimit64(int resource, const struct rlimit64 *rlim) {
    if (!rlim) {
        errno = EFAULT;
        return -1;
    }
    return wrap_int_result(setrlimit_impl(resource, (const struct rlimit *)rlim));
}

__attribute__((visibility("default"))) clock_t times(struct tms *buf) {
    return (clock_t)times_impl(buf);
}

__attribute__((visibility("default"))) int getrusage(int who, struct rusage *usage) {
    return wrap_int_result(getrusage_impl(who, usage));
}

__attribute__((visibility("default"))) int prlimit(int32_t pid, int resource, const struct rlimit *new_limit,
                                                   struct rlimit *old_limit) {
    return wrap_int_result(prlimit_impl(pid, resource, new_limit, old_limit));
}
