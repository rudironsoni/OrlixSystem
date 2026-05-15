#include "OrlixKernel.h"

int arch_boot_handoff_count(void);
const struct boot_params *arch_boot_last_params(void);
void arch_boot_reset_handoff(void);

static int expect_invalid_boot(const struct boot_params *params)
{
    if (OrlixBoot(params) != -1) {
        return -1;
    }
    if (arch_boot_handoff_count() != 0) {
        return -2;
    }
    return 0;
}

int main(void)
{
    char memory[4096];
    char initrd[64];
    char dtb[64];
    struct boot_params params = {
        .cmdline = "root=/dev/orlix-root console=ttyO0",
        .memory_base = (unsigned long)memory,
        .memory_size = sizeof(memory),
        .initrd_base = initrd,
        .initrd_size = sizeof(initrd),
        .dtb_base = dtb,
        .dtb_size = sizeof(dtb),
        .root_device = "/dev/orlix-root",
        .console_device = "ttyO0",
        .flags = 0,
    };

    arch_boot_reset_handoff();
    if (expect_invalid_boot(0) != 0) {
        return 1;
    }

    params.cmdline = 0;
    if (expect_invalid_boot(&params) != 0) {
        return 2;
    }
    params.cmdline = "root=/dev/orlix-root console=ttyO0";

    params.memory_base = 0;
    if (expect_invalid_boot(&params) != 0) {
        return 3;
    }
    params.memory_base = (unsigned long)memory;

    params.memory_size = 0;
    if (expect_invalid_boot(&params) != 0) {
        return 4;
    }
    params.memory_size = sizeof(memory);

    params.root_device = "";
    if (expect_invalid_boot(&params) != 0) {
        return 5;
    }
    params.root_device = "/dev/orlix-root";

    params.console_device = 0;
    if (expect_invalid_boot(&params) != 0) {
        return 6;
    }
    params.console_device = "ttyO0";

    if (OrlixBoot(&params) != 0) {
        return 7;
    }
    if (arch_boot_handoff_count() != 1) {
        return 8;
    }
    if (arch_boot_last_params() != &params) {
        return 9;
    }

    return 0;
}
