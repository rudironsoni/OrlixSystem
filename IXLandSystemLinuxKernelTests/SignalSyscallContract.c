#include "SignalSyscallContract.h"

#include <asm/signal.h>
#include <asm/unistd.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "runtime/syscall.h"

int signal_syscall_contract_rt_sigaction_uses_linux_uapi_layout(void) {
    struct sigaction act;
    struct sigaction oldact;
    struct sigaction queried;
    long ret;

    memset(&act, 0, sizeof(act));
    memset(&oldact, 0, sizeof(oldact));
    memset(&queried, 0, sizeof(queried));
    act.sa_handler = (__sighandler_t)(uintptr_t)0x1000;
    act.sa_flags = SA_RESTART | SA_ONSTACK;
    act.sa_mask.sig[0] = 1ULL << (2 - 1);

    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGINT, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&oldact, sizeof(act.sa_mask), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGINT, 0,
                                (long)(uintptr_t)&queried, sizeof(queried.sa_mask), 0, 0);
    if (ret != 0 ||
        queried.sa_handler != act.sa_handler ||
        queried.sa_flags != act.sa_flags ||
        queried.sa_mask.sig[0] != act.sa_mask.sig[0]) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGKILL, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    if (ret != -EINVAL) {
        errno = EBUSY;
        return -1;
    }

    return 0;
}
