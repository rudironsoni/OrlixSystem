#define _XOPEN_SOURCE 700

#include "OrlixHostAdapter/runtime/trap.h"
#include "OrlixHostAdapter/runtime/host_tls.h"

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
#define ORLIX_ARM_ESR_EC_MASK 0xfc000000U
#define ORLIX_ARM_ESR_EC_SHIFT 26U
#define ORLIX_ARM_ESR_EC_IABT_LOWER 0x20U
#define ORLIX_ARM_ESR_EC_IABT_CURRENT 0x21U
#define ORLIX_ARM_ESR_EC_DABT_LOWER 0x24U
#define ORLIX_ARM_ESR_EC_DABT_CURRENT 0x25U
#define ORLIX_ARM_ESR_DABT_WNR (1U << 6)
#define ORLIX_ARM64_MRS_TPIDR_EL0 0xd53bd040U
#define ORLIX_ARM64_MRS_TPIDR_EL0_MASK 0xffffffe0U
#define ORLIX_ARM64_LOAD_STORE_CLASS_SHIFT 25U
#define ORLIX_ARM64_LOAD_STORE_CLASS_MASK 0x7U
#define ORLIX_ARM64_LOAD_STORE_CLASS_VALUE 0x4U
#define ORLIX_ARM64_REGISTER_MASK 0x1fU
#define ORLIX_ARM64_TLS_MRS_SEARCH_INSTRUCTIONS 4U

struct OrlixHostUserTrapState {
    unsigned long user_base;
    unsigned long user_limit;
    unsigned long syscall_gate;
    unsigned long syscall_gate_limit;
    const unsigned long *kernel_sp;
    const unsigned long *active_user_tls;
    unsigned long *user_active;
    orlix_host_user_trap_entry_t entry;
    thread_act_t target_thread;
    unsigned long host_tls;
};

static struct OrlixHostUserTrapState OrlixHostUserTrap;
static struct orlix_host_user_trap_frame OrlixHostUserTrapFrame;
static struct orlix_host_user_trap_frame OrlixHostUserResumeFrame;
static atomic_bool OrlixHostUserResumePending;
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
    return OrlixHostUserTrap.user_base < OrlixHostUserTrap.user_limit &&
           tls >= OrlixHostUserTrap.user_base &&
           tls < OrlixHostUserTrap.user_limit;
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

__attribute__((naked))
static void OrlixHostUserTimerEntry(void)
{
    __asm__ volatile(
        "msr tpidr_el0, x3\n"
        "isb\n"
        "br x2\n");
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

static bool OrlixHostUserTrapMemoryBaseRegister(uint32_t instruction,
                                                unsigned int *base_register)
{
    unsigned int instruction_class =
        (instruction >> ORLIX_ARM64_LOAD_STORE_CLASS_SHIFT) &
        ORLIX_ARM64_LOAD_STORE_CLASS_MASK;
    unsigned int reg = (instruction >> 5U) & ORLIX_ARM64_REGISTER_MASK;

    if (instruction_class != ORLIX_ARM64_LOAD_STORE_CLASS_VALUE || reg == 31U) {
        return false;
    }

    *base_register = reg;
    return true;
}

static bool OrlixHostUserTrapRepairUserTlsLoad(mcontext_t machine_context,
                                               unsigned long fault_address)
{
    unsigned long active_tls;
    unsigned long user_pc = (unsigned long)machine_context->__ss.__pc;
    uint32_t faulting_instruction;
    unsigned int base_register;

    if (fault_address >= OrlixHostUserTrap.user_base &&
        fault_address < OrlixHostUserTrap.user_limit) {
        return false;
    }

    active_tls = OrlixHostUserTrapActiveTls();
    if (!active_tls) {
        return false;
    }

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
    if (!OrlixHostUserTrapMemoryBaseRegister(faulting_instruction, &base_register)) {
        return false;
    }

    for (unsigned int offset = 1;
         offset <= ORLIX_ARM64_TLS_MRS_SEARCH_INSTRUCTIONS;
         offset++) {
        unsigned long instruction_address = user_pc - offset * sizeof(uint32_t);
        uint32_t instruction;

        if (instruction_address < OrlixHostUserTrap.user_base) {
            break;
        }

        instruction = *(const uint32_t *)instruction_address;
        if ((instruction & ORLIX_ARM64_MRS_TPIDR_EL0_MASK) ==
                ORLIX_ARM64_MRS_TPIDR_EL0 &&
            (instruction & ORLIX_ARM64_REGISTER_MASK) == base_register) {
            if (!OrlixHostUserTrapWriteRegister(machine_context,
                                                base_register,
                                                active_tls)) {
                return false;
            }
            OrlixHostWriteTls(active_tls);
            return true;
        }
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

static void OrlixHostUserTrapSaveMachFrame(const arm_thread_state64_t *state,
                                           const arm_neon_state64_t *neon)
{
    for (unsigned int index = 0; index < 29; index++) {
        OrlixHostUserTrapFrame.regs[index] = (unsigned long)state->__x[index];
    }
    OrlixHostUserTrapFrame.regs[29] = (unsigned long)state->__fp;
    OrlixHostUserTrapFrame.regs[30] = (unsigned long)state->__lr;
    OrlixHostUserTrapFrame.sp = (unsigned long)state->__sp;
    OrlixHostUserTrapFrame.pc = (unsigned long)state->__pc;
    OrlixHostUserTrapFrame.pstate = (unsigned long)state->__cpsr;
    OrlixHostUserTrapFrame.fault_address = 0;
    OrlixHostUserTrapFrame.fault_flags = 0;
    OrlixHostUserTrapFrame.user_tls = 0;
    OrlixHostUserTrapFrame.frame_flags = 0;
    OrlixHostUserTrapSaveNeon(neon);
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
    unsigned long user_tls;

    if (!user_context || !user_context->uc_mcontext) {
        OrlixHostUserTrapReraise(signal_number);
        return;
    }

    machine_context = user_context->uc_mcontext;
    user_pc = (unsigned long)machine_context->__ss.__pc;
    if (!OrlixHostUserTrapContains(user_pc)) {
        OrlixHostUserTrapReraise(signal_number);
        return;
    }

    fault_address = (unsigned long)machine_context->__es.__far;
    if (!fault_address && info) {
        fault_address = (unsigned long)info->si_addr;
    }
    if (OrlixHostUserTrapRepairUserTlsLoad(machine_context, fault_address)) {
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
    OrlixHostUserTrapFrame.fault_flags =
        OrlixHostUserFaultFlags(signal_number, machine_context);
    OrlixHostUserTrapSetFrameTls(user_tls);

    machine_context->__ss.__x[0] = (uint64_t)trap_number;
    machine_context->__ss.__x[1] = (uint64_t)&OrlixHostUserTrapFrame;
    machine_context->__ss.__pc = (uint64_t)OrlixHostUserTrap.entry;
    machine_context->__ss.__sp = (uint64_t)kernel_sp;
}

static void OrlixHostUserTimerSleep(unsigned long long delay_ns)
{
    struct timespec delay = {
        .tv_sec = (time_t)(delay_ns / 1000000000ULL),
        .tv_nsec = (long)(delay_ns % 1000000000ULL),
    };

    while (nanosleep(&delay, &delay) == -1) {
    }
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
        __atomic_store_n(OrlixHostUserTrap.user_active, 1, __ATOMIC_RELEASE);
    }

    atomic_store_explicit(&OrlixHostUserResumePending, false,
                          memory_order_release);

    (void)thread_resume(OrlixHostUserTrap.target_thread);
    return status == KERN_SUCCESS;
}

static bool OrlixHostUserTimerRedirect(arm_thread_state64_t *state)
{
    arm_neon_state64_t neon;
    mach_msg_type_number_t neon_count = ARM_NEON_STATE64_COUNT;
    unsigned long active_tls;
    unsigned long kernel_sp;
    unsigned long user_pc = (unsigned long)state->__pc;
    kern_return_t status;

    if (!OrlixHostUserTrapContains(user_pc) ||
        OrlixHostUserTrapInSyscallGate(user_pc)) {
        return false;
    }

    active_tls = OrlixHostUserTrapActiveTls();
    if (!active_tls) {
        return false;
    }

    kernel_sp = *OrlixHostUserTrap.kernel_sp;
    if (!kernel_sp) {
        return false;
    }

    status = thread_get_state(OrlixHostUserTrap.target_thread,
                              ARM_NEON_STATE64,
                              (thread_state_t)&neon,
                              &neon_count);
    if (status != KERN_SUCCESS || neon_count != ARM_NEON_STATE64_COUNT) {
        return false;
    }

    OrlixHostUserTrapSaveMachFrame(state, &neon);
    OrlixHostUserTrapSetFrameTls(active_tls);
    state->__x[0] = (uint64_t)ORLIX_HOST_USER_TRAP_TIMER;
    state->__x[1] = (uint64_t)&OrlixHostUserTrapFrame;
    state->__x[2] = (uint64_t)OrlixHostUserTrap.entry;
    state->__x[3] = (uint64_t)OrlixHostUserTrap.host_tls;
    state->__pc = (uint64_t)OrlixHostUserTimerEntry;
    state->__sp = (uint64_t)kernel_sp;
    return true;
}

static void *OrlixHostUserTimerMain(void *context)
{
    (void)context;

    for (;;) {
        arm_thread_state64_t state;
        mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        kern_return_t status;

        if (OrlixHostUserResumeRedirect()) {
            continue;
        }

        OrlixHostUserTimerSleep(OrlixHostUserTimerPeriodNs);
        if (!OrlixHostUserTrap.entry || !OrlixHostUserTrapIsUserActive()) {
            continue;
        }

        status = thread_suspend(OrlixHostUserTrap.target_thread);
        if (status != KERN_SUCCESS) {
            continue;
        }

        if (!OrlixHostUserTrapIsUserActive()) {
            (void)thread_resume(OrlixHostUserTrap.target_thread);
            continue;
        }

        status = thread_get_state(OrlixHostUserTrap.target_thread,
                                  ARM_THREAD_STATE64,
                                  (thread_state_t)&state,
                                  &count);
        if (status == KERN_SUCCESS && count == ARM_THREAD_STATE64_COUNT &&
            OrlixHostUserTimerRedirect(&state)) {
            __atomic_store_n(OrlixHostUserTrap.user_active, 0, __ATOMIC_RELEASE);
            status = thread_set_state(OrlixHostUserTrap.target_thread,
                                      ARM_THREAD_STATE64,
                                      (thread_state_t)&state,
                                      ARM_THREAD_STATE64_COUNT);
            if (status != KERN_SUCCESS) {
                __atomic_store_n(OrlixHostUserTrap.user_active, 1, __ATOMIC_RELEASE);
            }
        }

        (void)thread_resume(OrlixHostUserTrap.target_thread);
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
    const int signals[] = { SIGTRAP, SIGILL, SIGBUS, SIGSEGV, SIGABRT };
    struct sigaction action;

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
    OrlixHostUserTrap.target_thread = pthread_mach_thread_np(pthread_self());
    OrlixHostUserTrap.host_tls = OrlixHostReadTls();

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

    while (atomic_load_explicit(&OrlixHostUserResumePending,
                                memory_order_acquire)) {
        __asm__ volatile("yield");
    }

    (void)OrlixHostEnterHostTls();
    _exit(127);
}
