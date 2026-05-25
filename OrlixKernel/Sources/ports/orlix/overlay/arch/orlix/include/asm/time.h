/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_TIME_H
#define _ASM_ORLIX_TIME_H

void orlix_timer_poll(void);
unsigned long long orlix_timer_next_deadline_ns(void);

#endif /* _ASM_ORLIX_TIME_H */
