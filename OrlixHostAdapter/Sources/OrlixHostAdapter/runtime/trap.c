#define _XOPEN_SOURCE 700

#include "OrlixHostAdapter/runtime/trap.h"
#include "OrlixHostAdapter/runtime/host_tls.h"
#include "OrlixHostAdapter/runtime/trap_decode.h"
#include "internal/asm/host_trap.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <pthread.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define ORLIX_HOST_USER_TIMER_NS 1000000ULL
#define ORLIX_HOST_USER_TIMER_SIGNAL SIGUSR1
#define ORLIX_ARM_ESR_EC_MASK 0xfc000000U
#define ORLIX_ARM_ESR_EC_SHIFT 26U
#define ORLIX_ARM_ESR_EC_IABT_LOWER 0x20U
#define ORLIX_ARM_ESR_EC_IABT_CURRENT 0x21U
#define ORLIX_ARM_ESR_EC_DABT_LOWER 0x24U
#define ORLIX_ARM_ESR_EC_DABT_CURRENT 0x25U
#define ORLIX_ARM_ESR_DABT_WNR (1U << 6)
struct OrlixHostUserTrapState {
    unsigned long user_base;
    unsigned long user_limit;
    unsigned long syscall_gate;
    unsigned long syscall_gate_limit;
    const unsigned long *kernel_sp;
    const unsigned long *active_user_tls;
    unsigned long *user_active;
    orlix_host_user_trap_entry_t entry;
    pthread_t target_pthread;
    thread_act_t target_thread;
    unsigned long host_tls;
};

static struct OrlixHostUserTrapState OrlixHostUserTrap;
static struct orlix_host_user_trap_frame OrlixHostUserTrapFrame;
static struct orlix_host_user_trap_frame OrlixHostUserResumeFrame;
static atomic_bool OrlixHostUserResumePending;
static unsigned char OrlixHostUserTrapSignalStack[SIGSTKSZ * 4];
static pthread_mutex_t OrlixHostUserTimerMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t OrlixHostUserTimerCond = PTHREAD_COND_INITIALIZER;
static bool OrlixHostUserTrapInstalled;
static bool OrlixHostUserTimerStarted;
static unsigned long long OrlixHostUserTimerPeriodNs = ORLIX_HOST_USER_TIMER_NS;

static bool OrlixHostUserTrapContains(unsigned long pc)
{
    return OrlixHostUserTrap.entry &&
           OrlixHostUserTrap.kernel_sp &&
           OrlixHostUserTrap.user_base < OrlixHostUserTrap.user_limit &&
           pc >= OrlixHostUserTrap.user_base &&
           pc < OrlixHostUserTrap.user_limit;
}

static bool OrlixHostUserTrapValidUserTls(unsigned long tls)
{
    return orlix_host_user_trap_valid_user_tls(OrlixHostUserTrap.user_base,
                                               OrlixHostUserTrap.user_limit,
                                               tls);
}

static bool OrlixHostUserTrapIsUserActive(void)
{
    return OrlixHostUserTrap.user_active &&
           __atomic_load_n(OrlixHostUserTrap.user_active, __ATOMIC_ACQUIRE);
}

static bool OrlixHostUserTrapInSyscallGate(unsigned long pc)
{
    return OrlixHostUserTrap.syscall_gate < OrlixHostUserTrap.syscall_gate_limit &&
           pc >= OrlixHostUserTrap.syscall_gate &&
           pc < OrlixHostUserTrap.syscall_gate_limit;
}

static bool OrlixHostUserTrapInstructionAt(unsigned long pc,
                                           uint32_t expected)
{
    const uint32_t *instruction;

    if (pc < OrlixHostUserTrap.user_base ||
        pc > OrlixHostUserTrap.user_limit - sizeof(*instruction)) {
        return false;
    }

    instruction = (const uint32_t *)pc;
    return *instruction == expected;
}

static bool OrlixHostUserTrapIsLinuxSyscallTrap(mcontext_t machine_context)
{
    unsigned long pc = (unsigned long)machine_context->__ss.__pc;

    if (OrlixHostUserTrapInstructionAt(pc,
                                       ORLIX_HOST_AARCH64_SYSCALL_BRK_INSN)) {
        return true;
    }

    if (pc >= OrlixHostUserTrap.user_base + sizeof(uint32_t) &&
        OrlixHostUserTrapInstructionAt(pc - sizeof(uint32_t),
                                       ORLIX_HOST_AARCH64_SYSCALL_BRK_INSN)) {
        machine_context->__ss.__pc = (uint64_t)(pc - sizeof(uint32_t));
        return true;
    }

    return false;
}

static unsigned long OrlixHostUserTrapTlsResumeTrampoline(void)
{
    unsigned long gate_size;
    unsigned long trampoline_size = sizeof(uint32_t) * 3UL;

    if (OrlixHostUserTrap.syscall_gate >= OrlixHostUserTrap.syscall_gate_limit) {
        return 0;
    }

    gate_size = OrlixHostUserTrap.syscall_gate_limit - OrlixHostUserTrap.syscall_gate;
    if (gate_size < ORLIX_HOST_USER_TRAP_TLS_RESUME_OFFSET + trampoline_size) {
        return 0;
    }

    return OrlixHostUserTrap.syscall_gate + ORLIX_HOST_USER_TRAP_TLS_RESUME_OFFSET;
}

static unsigned long OrlixHostUserTrapActiveTls(void)
{
    unsigned long tls;

    if (!OrlixHostUserTrap.active_user_tls) {
        return 0;
    }

    tls = __atomic_load_n(OrlixHostUserTrap.active_user_tls, __ATOMIC_ACQUIRE);
    if (OrlixHostUserTrapValidUserTls(tls)) {
        return tls;
    }
    return 0;
}

static unsigned long OrlixHostReadTls(void)
{
    unsigned long tls;

    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tls));
    return tls;
}

static void OrlixHostWriteTls(unsigned long tls)
{
    __asm__ volatile(
        "msr tpidr_el0, %0\n"
        "isb\n"
        :
        : "r"(tls)
        : "memory");
}

static unsigned long OrlixHostUserTrapCurrentTls(void)
{
    unsigned long tls = OrlixHostReadTls();
    unsigned long active_tls;

    if (OrlixHostUserTrapValidUserTls(tls)) {
        return tls;
    }

    if (OrlixHostUserTrap.host_tls && tls == OrlixHostUserTrap.host_tls) {
        active_tls = OrlixHostUserTrapActiveTls();
        if (active_tls) {
            return active_tls;
        }
    }

    return tls;
}

static bool OrlixHostUserTrapWriteRegister(mcontext_t machine_context,
                                           unsigned int reg,
                                           unsigned long value)
{
    if (reg < 29) {
        machine_context->__ss.__x[reg] = value;
        return true;
    }
    if (reg == 29) {
        machine_context->__ss.__fp = value;
        return true;
    }
    if (reg == 30) {
        machine_context->__ss.__lr = value;
        return true;
    }
    return false;
}

static bool OrlixHostUserTrapReadRegister(mcontext_t machine_context,
                                          unsigned int reg,
                                          unsigned long *value)
{
    if (!value) {
        return false;
    }
    if (reg < 29) {
        *value = machine_context->__ss.__x[reg];
        return true;
    }
    if (reg == 29) {
        *value = machine_context->__ss.__fp;
        return true;
    }
    if (reg == 30) {
        *value = machine_context->__ss.__lr;
        return true;
    }
    return false;
}

static bool OrlixHostUserTrapRepairUserTlsLoad(mcontext_t machine_context,
                                               unsigned long fault_address)
{
    unsigned long active_tls;
    unsigned long host_tls;
    unsigned long user_pc = (unsigned long)machine_context->__ss.__pc;
    uint32_t faulting_instruction;
    unsigned int base_register;
    unsigned long base_value;
    unsigned long rebased_value;

    if (fault_address >= OrlixHostUserTrap.user_base &&
        fault_address < OrlixHostUserTrap.user_limit) {
        return false;
    }

    if (!OrlixHostUserTrap.active_user_tls) {
        return false;
    }
    active_tls = __atomic_load_n(OrlixHostUserTrap.active_user_tls,
                                 __ATOMIC_ACQUIRE);
    if (!orlix_host_user_trap_can_repair_user_tls(OrlixHostUserTrap.user_base,
                                                  OrlixHostUserTrap.user_limit,
                                                  active_tls)) {
        return false;
    }
    host_tls = orlix_host_user_trap_host_tls_reference(
        OrlixHostUserTrap.host_tls,
        OrlixHostReadTls(),
        OrlixHostUserTrap.user_base,
        OrlixHostUserTrap.user_limit);

    if (user_pc < OrlixHostUserTrap.user_base + sizeof(uint32_t) ||
        user_pc >= OrlixHostUserTrap.user_limit) {
        return false;
    }

    /*
     * Darwin signal frames expose general registers but not TPIDR_EL0. If host
     * delivery loses Linux user TLS, retry the faulting load with the TPIDR
     * destination register repaired to the kernel-owned active user TLS.
     */
    faulting_instruction = *(const uint32_t *)user_pc;
    if (!orlix_host_user_trap_memory_base_register(faulting_instruction,
                                                   &base_register)) {
        return false;
    }

    for (unsigned int offset = 1;
         offset <= ORLIX_HOST_USER_TRAP_TLS_MRS_SEARCH_INSTRUCTIONS;
         offset++) {
        unsigned long instruction_address = user_pc - offset * sizeof(uint32_t);
        uint32_t instruction;
        unsigned int destination_register;

        if (instruction_address < OrlixHostUserTrap.user_base) {
            break;
        }

        instruction = *(const uint32_t *)instruction_address;
        if (orlix_host_user_trap_mrs_tpidr_el0_destination(
                instruction,
                &destination_register) &&
            destination_register == base_register) {
            if (!OrlixHostUserTrapWriteRegister(machine_context,
                                                base_register,
                                                active_tls)) {
                return false;
            }
            OrlixHostWriteTls(active_tls);
            return true;
        }
        if (orlix_host_user_trap_integer_instruction_writes_register(
                instruction,
                base_register)) {
            break;
        }
    }

    if (OrlixHostUserTrapReadRegister(machine_context,
                                      base_register,
                                      &base_value) &&
        orlix_host_user_trap_rebase_register_from_host_tls(host_tls,
                                                           active_tls,
                                                           base_value,
                                                           OrlixHostUserTrap.user_base,
                                                           OrlixHostUserTrap.user_limit,
                                                           &rebased_value) &&
        OrlixHostUserTrapWriteRegister(machine_context,
                                       base_register,
                                       rebased_value)) {
        OrlixHostWriteTls(active_tls);
        return true;
    }

    return false;
}

unsigned long OrlixHostEnterHostTls(void)
{
    unsigned long active_tls = OrlixHostReadTls();

    if (OrlixHostUserTrap.host_tls && active_tls != OrlixHostUserTrap.host_tls) {
        OrlixHostWriteTls(OrlixHostUserTrap.host_tls);
    }
    return active_tls;
}

void OrlixHostLeaveHostTls(unsigned long active_tls)
{
    if (OrlixHostUserTrap.host_tls && active_tls != OrlixHostUserTrap.host_tls) {
        OrlixHostWriteTls(active_tls);
    }
}

static unsigned long OrlixHostUserFaultFlags(int signal_number,
                                             mcontext_t machine_context)
{
    unsigned int esr = machine_context->__es.__esr;
    unsigned int ec = (esr & ORLIX_ARM_ESR_EC_MASK) >> ORLIX_ARM_ESR_EC_SHIFT;
    unsigned long flags = 0;

    if (signal_number == SIGBUS) {
        flags |= ORLIX_HOST_USER_FAULT_BUS;
    }
    if (ec == ORLIX_ARM_ESR_EC_IABT_LOWER ||
        ec == ORLIX_ARM_ESR_EC_IABT_CURRENT) {
        flags |= ORLIX_HOST_USER_FAULT_EXEC;
    }
    if ((ec == ORLIX_ARM_ESR_EC_DABT_LOWER ||
         ec == ORLIX_ARM_ESR_EC_DABT_CURRENT) &&
        (esr & ORLIX_ARM_ESR_DABT_WNR)) {
        flags |= ORLIX_HOST_USER_FAULT_WRITE;
    }

    return flags;
}

static void OrlixHostUserTrapReraise(int signal_number)
{
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

static void OrlixHostUserTrapSaveNeon(const arm_neon_state64_t *neon)
{
    for (unsigned int index = 0; index < 32; index++) {
        __uint128_t value = neon->__v[index];

        OrlixHostUserTrapFrame.simd[index * 2] = (uint64_t)value;
        OrlixHostUserTrapFrame.simd[index * 2 + 1] = (uint64_t)(value >> 64);
    }

    OrlixHostUserTrapFrame.fpsr = neon->__fpsr;
    OrlixHostUserTrapFrame.fpcr = neon->__fpcr;
    OrlixHostUserTrapFrame.frame_flags |= ORLIX_HOST_USER_FRAME_HAS_SIMD;
}

static void OrlixHostUserTrapLoadNeon(
    arm_neon_state64_t *neon,
    const struct orlix_host_user_trap_frame *frame)
{
    for (unsigned int index = 0; index < 32; index++) {
        neon->__v[index] =
            ((__uint128_t)frame->simd[index * 2 + 1] << 64) |
            (__uint128_t)frame->simd[index * 2];
    }

    neon->__fpsr = (uint32_t)frame->fpsr;
    neon->__fpcr = (uint32_t)frame->fpcr;
}

static void OrlixHostUserTrapLoadMachFrame(
    arm_thread_state64_t *state,
    const struct orlix_host_user_trap_frame *frame)
{
    for (unsigned int index = 0; index < 29; index++) {
        state->__x[index] = (uint64_t)frame->regs[index];
    }

    state->__fp = (uint64_t)frame->regs[29];
    state->__lr = (uint64_t)frame->regs[30];
    state->__sp = (uint64_t)frame->sp;
    state->__pc = (uint64_t)frame->pc;
    state->__cpsr = (uint32_t)frame->pstate;
    state->__pad = 0;

    if ((frame->frame_flags & ORLIX_HOST_USER_FRAME_SYSCALL_RETURN) &&
        (frame->frame_flags & ORLIX_HOST_USER_FRAME_HAS_TLS) &&
        OrlixHostUserTrapValidUserTls(frame->user_tls)) {
        unsigned long trampoline = OrlixHostUserTrapTlsResumeTrampoline();

        if (trampoline) {
            state->__x[16] = (uint64_t)frame->user_tls;
            state->__x[17] = (uint64_t)frame->pc;
            state->__pc = (uint64_t)trampoline;
        }
    }
}

static void OrlixHostUserTrapSaveFrame(mcontext_t machine_context)
{
    for (unsigned int index = 0; index < 29; index++) {
        OrlixHostUserTrapFrame.regs[index] = (unsigned long)machine_context->__ss.__x[index];
    }
    OrlixHostUserTrapFrame.regs[29] = (unsigned long)machine_context->__ss.__fp;
    OrlixHostUserTrapFrame.regs[30] = (unsigned long)machine_context->__ss.__lr;
    OrlixHostUserTrapFrame.sp = (unsigned long)machine_context->__ss.__sp;
    OrlixHostUserTrapFrame.pc = (unsigned long)machine_context->__ss.__pc;
    OrlixHostUserTrapFrame.pstate = (unsigned long)machine_context->__ss.__cpsr;
    OrlixHostUserTrapFrame.fault_address = 0;
    OrlixHostUserTrapFrame.fault_flags = 0;
    OrlixHostUserTrapFrame.user_tls = 0;
    OrlixHostUserTrapFrame.frame_flags = 0;
    OrlixHostUserTrapSaveNeon(&machine_context->__ns);
}

static void OrlixHostUserTrapSetFrameTls(unsigned long user_tls)
{
    OrlixHostUserTrapFrame.user_tls = user_tls;
    OrlixHostUserTrapFrame.frame_flags |= ORLIX_HOST_USER_FRAME_HAS_TLS;
}

static bool OrlixHostUserTrapIsMemoryFaultSignal(int signal_number)
{
    return signal_number == SIGBUS || signal_number == SIGSEGV;
}

static unsigned long OrlixHostUserTrapFaultAddress(unsigned long user_pc,
                                                   unsigned long fault_flags,
                                                   siginfo_t *info,
                                                   mcontext_t machine_context)
{
    unsigned long fault_address;

    if (fault_flags & ORLIX_HOST_USER_FAULT_EXEC) {
        return user_pc;
    }

    fault_address = (unsigned long)machine_context->__es.__far;
    if (!fault_address && info) {
        fault_address = (unsigned long)info->si_addr;
    }
    return fault_address;
}

static void OrlixHostUserTrapHandler(int signal_number,
                                     siginfo_t *info,
                                     void *context)
{
    ucontext_t *user_context = (ucontext_t *)context;
    mcontext_t machine_context;
    int trap_number = signal_number;
    unsigned long user_pc;
    unsigned long kernel_sp;
    unsigned long fault_address;
    unsigned long fault_flags;
    unsigned long user_tls;
    bool timer_signal = signal_number == ORLIX_HOST_USER_TIMER_SIGNAL;

    if (!user_context || !user_context->uc_mcontext) {
        if (!timer_signal) {
            OrlixHostUserTrapReraise(signal_number);
        }
        return;
    }

    machine_context = user_context->uc_mcontext;
    user_pc = (unsigned long)machine_context->__ss.__pc;
    if (!OrlixHostUserTrapContains(user_pc) ||
        OrlixHostUserTrapInSyscallGate(user_pc)) {
        if (timer_signal) {
            return;
        }
        OrlixHostUserTrapReraise(signal_number);
        return;
    }
    if (timer_signal) {
        trap_number = ORLIX_HOST_USER_TRAP_TIMER;
    } else if (signal_number == SIGTRAP &&
               OrlixHostUserTrapIsLinuxSyscallTrap(machine_context)) {
        trap_number = ORLIX_HOST_USER_TRAP_SYSCALL;
    }

    fault_flags = OrlixHostUserFaultFlags(signal_number, machine_context);
    fault_address = OrlixHostUserTrapFaultAddress(user_pc, fault_flags, info,
                                                  machine_context);
    if (OrlixHostUserTrapIsMemoryFaultSignal(signal_number) &&
        OrlixHostUserTrapRepairUserTlsLoad(machine_context, fault_address)) {
        return;
    }

    user_tls = OrlixHostUserTrapCurrentTls();
    OrlixHostWriteTls(OrlixHostUserTrap.host_tls);
    __atomic_store_n(OrlixHostUserTrap.user_active, 0, __ATOMIC_RELEASE);

    kernel_sp = *OrlixHostUserTrap.kernel_sp;
    if (!kernel_sp) {
        _exit(128 + signal_number);
    }

    OrlixHostUserTrapSaveFrame(machine_context);
    OrlixHostUserTrapFrame.fault_address = fault_address;
    OrlixHostUserTrapFrame.fault_flags = fault_flags;
    OrlixHostUserTrapSetFrameTls(user_tls);

    machine_context->__ss.__x[0] = (uint64_t)trap_number;
    machine_context->__ss.__x[1] = (uint64_t)&OrlixHostUserTrapFrame;
    machine_context->__ss.__pc = (uint64_t)OrlixHostUserTrap.entry;
    machine_context->__ss.__sp = (uint64_t)kernel_sp;
}

static void OrlixHostUserTimerSleep(unsigned long long delay_ns)
{
    struct timespec delay;

    if (atomic_load_explicit(&OrlixHostUserResumePending, memory_order_acquire)) {
        return;
    }

    delay.tv_sec = (time_t)(delay_ns / 1000000000ULL);
    delay.tv_nsec = (long)(delay_ns % 1000000000ULL);

    (void)pthread_mutex_lock(&OrlixHostUserTimerMutex);
    if (!atomic_load_explicit(&OrlixHostUserResumePending,
                              memory_order_acquire)) {
        (void)pthread_cond_timedwait_relative_np(&OrlixHostUserTimerCond,
                                                 &OrlixHostUserTimerMutex,
                                                 &delay);
    }
    (void)pthread_mutex_unlock(&OrlixHostUserTimerMutex);
}

static void OrlixHostUserTimerWake(void)
{
    (void)pthread_cond_signal(&OrlixHostUserTimerCond);
}

static bool OrlixHostUserResumeRedirect(void)
{
    arm_thread_state64_t state;
    arm_neon_state64_t neon;
    kern_return_t status;

    if (!atomic_load_explicit(&OrlixHostUserResumePending, memory_order_acquire)) {
        return false;
    }

    status = thread_suspend(OrlixHostUserTrap.target_thread);
    if (status != KERN_SUCCESS) {
        return false;
    }

    status = KERN_SUCCESS;
    if (OrlixHostUserResumeFrame.frame_flags & ORLIX_HOST_USER_FRAME_HAS_SIMD) {
        OrlixHostUserTrapLoadNeon(&neon, &OrlixHostUserResumeFrame);
        status = thread_set_state(OrlixHostUserTrap.target_thread,
                                  ARM_NEON_STATE64,
                                  (thread_state_t)&neon,
                                  ARM_NEON_STATE64_COUNT);
    }

    if (status == KERN_SUCCESS) {
        OrlixHostUserTrapLoadMachFrame(&state, &OrlixHostUserResumeFrame);
        status = thread_set_state(OrlixHostUserTrap.target_thread,
                                  ARM_THREAD_STATE64,
                                  (thread_state_t)&state,
                                  ARM_THREAD_STATE64_COUNT);
    }

    if (status == KERN_SUCCESS) {
        unsigned long resumed_tls = 0;

        if (OrlixHostUserResumeFrame.frame_flags & ORLIX_HOST_USER_FRAME_HAS_TLS) {
            resumed_tls = OrlixHostUserResumeFrame.user_tls;
        }
        __atomic_store_n(OrlixHostUserTrap.user_active,
                         OrlixHostUserTrapValidUserTls(resumed_tls) ? 1UL : 0UL,
                         __ATOMIC_RELEASE);
    }
    atomic_store_explicit(&OrlixHostUserResumePending, false,
                          memory_order_release);

    (void)thread_resume(OrlixHostUserTrap.target_thread);
    return status == KERN_SUCCESS;
}

static void *OrlixHostUserTimerMain(void *context)
{
    (void)context;

    for (;;) {
        if (OrlixHostUserResumeRedirect()) {
            continue;
        }

        OrlixHostUserTimerSleep(OrlixHostUserTimerPeriodNs);
        if (OrlixHostUserResumeRedirect()) {
            continue;
        }
        if (!OrlixHostUserTrap.entry || !OrlixHostUserTrapIsUserActive()) {
            continue;
        }

        (void)pthread_kill(OrlixHostUserTrap.target_pthread,
                           ORLIX_HOST_USER_TIMER_SIGNAL);
    }
}

__attribute__((visibility("hidden"))) int orlix_host_user_trap_install(
    unsigned long user_base,
    unsigned long user_limit,
    unsigned long syscall_gate,
    unsigned long syscall_gate_size,
    const unsigned long *kernel_sp,
    const unsigned long *active_user_tls,
    unsigned long *user_active,
    orlix_host_user_trap_entry_t entry)
{
    const int signals[] = { SIGTRAP, SIGILL, SIGBUS, SIGSEGV, SIGABRT,
                            ORLIX_HOST_USER_TIMER_SIGNAL };
    struct sigaction action;
    stack_t signal_stack;
    sigset_t timer_signal_set;

    if (!kernel_sp || !active_user_tls || !user_active || !entry ||
        user_base >= user_limit) {
        return -1;
    }

    OrlixHostUserTrap.user_base = user_base;
    OrlixHostUserTrap.user_limit = user_limit;
    OrlixHostUserTrap.syscall_gate = syscall_gate;
    OrlixHostUserTrap.syscall_gate_limit = syscall_gate + syscall_gate_size;
    OrlixHostUserTrap.kernel_sp = kernel_sp;
    OrlixHostUserTrap.active_user_tls = active_user_tls;
    OrlixHostUserTrap.user_active = user_active;
    OrlixHostUserTrap.entry = entry;
    OrlixHostUserTrap.target_pthread = pthread_self();
    OrlixHostUserTrap.target_thread = pthread_mach_thread_np(pthread_self());
    OrlixHostUserTrap.host_tls = OrlixHostReadTls();

    if (OrlixHostUserTrapInstalled) {
        return 0;
    }

    signal_stack.ss_sp = OrlixHostUserTrapSignalStack;
    signal_stack.ss_size = sizeof(OrlixHostUserTrapSignalStack);
    signal_stack.ss_flags = 0;
    if (sigaltstack(&signal_stack, NULL) != 0) {
        return -1;
    }

    sigemptyset(&action.sa_mask);
    action.sa_sigaction = OrlixHostUserTrapHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (unsigned int index = 0; index < sizeof(signals) / sizeof(signals[0]); index++) {
        if (sigaction(signals[index], &action, NULL) != 0) {
            return -1;
        }
    }

    sigemptyset(&timer_signal_set);
    sigaddset(&timer_signal_set, ORLIX_HOST_USER_TIMER_SIGNAL);
    pthread_sigmask(SIG_UNBLOCK, &timer_signal_set, NULL);

    OrlixHostUserTrapInstalled = true;
    return 0;
}

__attribute__((visibility("hidden"))) int orlix_host_user_trap_start_timer(unsigned long long period_ns)
{
    pthread_t timer_thread;
    unsigned long active_tls;
    int create_status;

    if (!OrlixHostUserTrapInstalled || OrlixHostUserTimerStarted) {
        return OrlixHostUserTrapInstalled ? 0 : -1;
    }

    if (period_ns) {
        OrlixHostUserTimerPeriodNs = period_ns;
    }

    active_tls = OrlixHostEnterHostTls();
    create_status = pthread_create(&timer_thread, NULL, OrlixHostUserTimerMain, NULL);
    OrlixHostLeaveHostTls(active_tls);

    if (create_status != 0) {
        return -1;
    }
    (void)pthread_detach(timer_thread);

    OrlixHostUserTimerStarted = true;
    return 0;
}

__attribute__((visibility("hidden"), noreturn)) void orlix_host_user_trap_resume(
    const struct orlix_host_user_trap_frame *frame)
{
    if (!frame) {
        (void)OrlixHostEnterHostTls();
        _exit(127);
    }

    OrlixHostUserResumeFrame = *frame;
    if (frame->frame_flags & ORLIX_HOST_USER_FRAME_HAS_TLS) {
        OrlixHostWriteTls(frame->user_tls);
    }
    atomic_store_explicit(&OrlixHostUserResumePending, true,
                          memory_order_release);
    OrlixHostUserTimerWake();

    while (atomic_load_explicit(&OrlixHostUserResumePending,
                                memory_order_acquire)) {
        __asm__ volatile("yield");
    }

    (void)OrlixHostEnterHostTls();
    _exit(127);
}
