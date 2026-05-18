/* SPDX-License-Identifier: GPL-2.0-only */

#define ORLIX_PROBE_USED __attribute__((used))

ORLIX_PROBE_USED __attribute__((section("__ORLIX,__init")))
void orlix_probe_init_text(void)
{
}

ORLIX_PROBE_USED __attribute__((section("__ORLIX,__initdata")))
unsigned long orlix_probe_init_data = 1;

ORLIX_PROBE_USED __attribute__((section("__ORLIX,__percpu")))
unsigned long orlix_probe_percpu = 2;

ORLIX_PROBE_USED __attribute__((section("__ORLIX,__roinit")))
const unsigned long orlix_probe_ro_after_init = 3;

ORLIX_PROBE_USED __attribute__((section("__ORLIX,__discard")))
void *orlix_probe_discard_addressable = (void *)&orlix_probe_init_text;
