/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_SIGNAL_H
#define _ASM_ORLIX_SIGNAL_H

#include <uapi/asm/signal.h>

struct pt_regs;

void orlix_do_signal_or_restart(struct pt_regs *regs);
void orlix_exit_to_user_mode_work(struct pt_regs *regs);

#endif /* _ASM_ORLIX_SIGNAL_H */
