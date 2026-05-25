/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ORLIX_INTERNAL_ASM_HOST_THREAD_H
#define _ORLIX_INTERNAL_ASM_HOST_THREAD_H

void orlix_host_thread_idle(void);
void orlix_host_thread_idle_until(unsigned long long deadline_ns);

#endif /* _ORLIX_INTERNAL_ASM_HOST_THREAD_H */
