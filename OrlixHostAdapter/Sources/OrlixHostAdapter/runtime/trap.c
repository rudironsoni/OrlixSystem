#include "OrlixHostAdapter/runtime/trap.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

struct OrlixHostUserTrapState {
    unsigned long user_base;
    unsigned long user_limit;
    const unsigned long *kernel_sp;
    orlix_host_user_trap_entry_t entry;
};

static struct OrlixHostUserTrapState OrlixHostUserTrap;
static bool OrlixHostUserTrapInstalled;

static bool OrlixHostUserTrapContains(unsigned long pc)
{
    return OrlixHostUserTrap.entry &&
           OrlixHostUserTrap.kernel_sp &&
           OrlixHostUserTrap.user_base < OrlixHostUserTrap.user_limit &&
           pc >= OrlixHostUserTrap.user_base &&
           pc < OrlixHostUserTrap.user_limit;
}

static void OrlixHostUserTrapReraise(int signal_number)
{
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

static void OrlixHostUserTrapHandler(int signal_number,
                                     siginfo_t *info,
                                     void *context)
{
    ucontext_t *user_context = (ucontext_t *)context;
    mcontext_t machine_context;
    unsigned long user_pc;
    unsigned long user_sp;
    unsigned long kernel_sp;

    (void)info;

    if (!user_context || !user_context->uc_mcontext) {
        OrlixHostUserTrapReraise(signal_number);
        return;
    }

    machine_context = user_context->uc_mcontext;
    user_pc = (unsigned long)machine_context->__ss.__pc;
    user_sp = (unsigned long)machine_context->__ss.__sp;
    if (!OrlixHostUserTrapContains(user_pc)) {
        OrlixHostUserTrapReraise(signal_number);
        return;
    }

    kernel_sp = *OrlixHostUserTrap.kernel_sp;
    if (!kernel_sp) {
        _exit(128 + signal_number);
    }

    machine_context->__ss.__x[0] = (uint64_t)signal_number;
    machine_context->__ss.__x[1] = (uint64_t)user_pc;
    machine_context->__ss.__x[2] = (uint64_t)user_sp;
    machine_context->__ss.__pc = (uint64_t)OrlixHostUserTrap.entry;
    machine_context->__ss.__sp = (uint64_t)kernel_sp;
}

__attribute__((visibility("hidden"))) int orlix_host_user_trap_install(
    unsigned long user_base,
    unsigned long user_limit,
    const unsigned long *kernel_sp,
    orlix_host_user_trap_entry_t entry)
{
    const int signals[] = { SIGTRAP, SIGILL, SIGBUS, SIGSEGV, SIGABRT };
    struct sigaction action;

    if (!kernel_sp || !entry || user_base >= user_limit) {
        return -1;
    }

    OrlixHostUserTrap.user_base = user_base;
    OrlixHostUserTrap.user_limit = user_limit;
    OrlixHostUserTrap.kernel_sp = kernel_sp;
    OrlixHostUserTrap.entry = entry;

    if (OrlixHostUserTrapInstalled) {
        return 0;
    }

    sigemptyset(&action.sa_mask);
    action.sa_sigaction = OrlixHostUserTrapHandler;
    action.sa_flags = SA_SIGINFO;
    for (unsigned int index = 0; index < sizeof(signals) / sizeof(signals[0]); index++) {
        if (sigaction(signals[index], &action, NULL) != 0) {
            return -1;
        }
    }

    OrlixHostUserTrapInstalled = true;
    return 0;
}
