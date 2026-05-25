/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ORLIX_INTERNAL_ASM_HOST_CONSOLE_H
#define _ORLIX_INTERNAL_ASM_HOST_CONSOLE_H

void orlix_host_console_write(const void *bytes, unsigned long length);
unsigned long orlix_host_console_read_input(void *bytes, unsigned long length);

#endif
