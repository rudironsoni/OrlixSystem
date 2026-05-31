#ifndef ORLIX_HOST_ADAPTER_RUNTIME_TRAP_DECODE_H
#define ORLIX_HOST_ADAPTER_RUNTIME_TRAP_DECODE_H

#include <stdbool.h>
#include <stdint.h>

#define ORLIX_HOST_USER_TRAP_ARM64_MRS_TPIDR_EL0 0xd53bd040U
#define ORLIX_HOST_USER_TRAP_ARM64_MRS_TPIDR_EL0_MASK 0xffffffe0U
#define ORLIX_HOST_USER_TRAP_ARM64_LOAD_STORE_CLASS_SHIFT 25U
#define ORLIX_HOST_USER_TRAP_ARM64_LOAD_STORE_CLASS_MASK 0x7U
#define ORLIX_HOST_USER_TRAP_ARM64_LOAD_STORE_CLASS_VALUE 0x4U
#define ORLIX_HOST_USER_TRAP_ARM64_REGISTER_MASK 0x1fU
#define ORLIX_HOST_USER_TRAP_TLS_MRS_SEARCH_INSTRUCTIONS 32U
#define ORLIX_HOST_USER_TRAP_TLS_REBASE_WINDOW 4096UL

static inline bool orlix_host_user_trap_valid_user_tls(unsigned long user_base,
                                                       unsigned long user_limit,
                                                       unsigned long tls)
{
    return user_base < user_limit &&
           tls >= user_base &&
           tls < user_limit &&
           (tls & (sizeof(uintptr_t) - 1U)) == 0;
}

static inline unsigned long orlix_host_user_trap_host_tls_reference(
    unsigned long installed_host_tls,
    unsigned long live_tls,
    unsigned long user_base,
    unsigned long user_limit)
{
    if (live_tls &&
        !orlix_host_user_trap_valid_user_tls(user_base, user_limit, live_tls)) {
        return live_tls;
    }
    return installed_host_tls;
}

static inline bool orlix_host_user_trap_memory_base_register(uint32_t instruction,
                                                             unsigned int *base_register)
{
    unsigned int instruction_class =
        (instruction >> ORLIX_HOST_USER_TRAP_ARM64_LOAD_STORE_CLASS_SHIFT) &
        ORLIX_HOST_USER_TRAP_ARM64_LOAD_STORE_CLASS_MASK;
    unsigned int reg =
        (instruction >> 5U) & ORLIX_HOST_USER_TRAP_ARM64_REGISTER_MASK;

    if (!base_register ||
        instruction_class != ORLIX_HOST_USER_TRAP_ARM64_LOAD_STORE_CLASS_VALUE ||
        reg == 31U) {
        return false;
    }

    *base_register = reg;
    return true;
}

static inline bool orlix_host_user_trap_mrs_tpidr_el0_destination(
    uint32_t instruction,
    unsigned int *destination_register)
{
    if (!destination_register ||
        (instruction & ORLIX_HOST_USER_TRAP_ARM64_MRS_TPIDR_EL0_MASK) !=
            ORLIX_HOST_USER_TRAP_ARM64_MRS_TPIDR_EL0) {
        return false;
    }

    *destination_register =
        instruction & ORLIX_HOST_USER_TRAP_ARM64_REGISTER_MASK;
    return true;
}

static inline bool orlix_host_user_trap_integer_instruction_writes_register(
    uint32_t instruction,
    unsigned int reg)
{
    if (reg == 31U ||
        (instruction & ORLIX_HOST_USER_TRAP_ARM64_REGISTER_MASK) != reg) {
        return false;
    }

    if ((instruction & 0x1f000000U) == 0x10000000U) {
        return true; /* ADR/ADRP */
    }
    if ((instruction & 0x1f000000U) == 0x11000000U) {
        return true; /* ADD/SUB immediate */
    }
    if ((instruction & 0x1f800000U) == 0x12000000U) {
        return true; /* logical immediate */
    }
    if ((instruction & 0x1f800000U) == 0x12800000U) {
        return true; /* move wide immediate */
    }
    if ((instruction & 0x1f800000U) == 0x13000000U) {
        return true; /* bitfield */
    }
    if ((instruction & 0x1f800000U) == 0x13800000U) {
        return true; /* extract */
    }
    if ((instruction & 0x1f200000U) == 0x0a000000U) {
        return true; /* logical shifted register */
    }
    if ((instruction & 0x1f200000U) == 0x0b000000U) {
        return true; /* ADD/SUB shifted register */
    }

    return false;
}

static inline bool orlix_host_user_trap_recent_tpidr_write_matches_base(
    const uint32_t *instructions,
    unsigned long instruction_count,
    unsigned long faulting_index,
    unsigned int base_register)
{
    if (!instructions ||
        faulting_index >= instruction_count ||
        base_register == 31U) {
        return false;
    }

    for (unsigned int offset = 1;
         offset <= ORLIX_HOST_USER_TRAP_TLS_MRS_SEARCH_INSTRUCTIONS &&
         offset <= faulting_index;
         offset++) {
        unsigned int destination_register = 0;

        uint32_t instruction = instructions[faulting_index - offset];

        if (orlix_host_user_trap_mrs_tpidr_el0_destination(
                instruction,
                &destination_register) &&
            destination_register == base_register) {
            return true;
        }
        if (orlix_host_user_trap_integer_instruction_writes_register(
                instruction,
                base_register)) {
            return false;
        }
    }

    return false;
}

static inline bool orlix_host_user_trap_rebase_register_from_host_tls(
    unsigned long host_tls,
    unsigned long active_user_tls,
    unsigned long register_value,
    unsigned long user_base,
    unsigned long user_limit,
    unsigned long *rebased_value)
{
    unsigned long delta;
    unsigned long corrected;

    if (!host_tls || !active_user_tls || !rebased_value ||
        user_base >= user_limit ||
        active_user_tls < user_base ||
        active_user_tls >= user_limit ||
        (register_value >= user_base && register_value < user_limit)) {
        return false;
    }

    if (host_tls >= register_value) {
        delta = host_tls - register_value;
        if (delta > ORLIX_HOST_USER_TRAP_TLS_REBASE_WINDOW ||
            active_user_tls < delta) {
            return false;
        }
        corrected = active_user_tls - delta;
    } else {
        delta = register_value - host_tls;
        if (delta > ORLIX_HOST_USER_TRAP_TLS_REBASE_WINDOW ||
            active_user_tls > UINT64_MAX - delta) {
            return false;
        }
        corrected = active_user_tls + delta;
    }

    if (corrected < user_base || corrected >= user_limit) {
        return false;
    }

    *rebased_value = corrected;
    return true;
}

#endif /* ORLIX_HOST_ADAPTER_RUNTIME_TRAP_DECODE_H */
