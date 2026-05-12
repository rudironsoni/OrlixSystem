#include <asm/unistd.h>
#include <linux/net.h>

#include "runtime/syscall.h"

long socketpair_stream_syscall(int fds[2]) {
    return syscall_dispatch_impl(__NR_socketpair, AF_UNIX, SOCK_STREAM, 0,
                                 (long)(uintptr_t)fds, 0, 0);
}
