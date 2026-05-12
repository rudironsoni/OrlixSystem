/*
 * OrlixKernel Random Subsystem - Darwin Bridge
 *
 * This file provides the host entropy intake for Linux-shaped random owners.
 * The kernel-facing contract is declared by OrlixKernel; this file only
 * implements that narrow private seam.
 */

#include <stddef.h>
#include <stdlib.h>

void get_random_bytes(void *buf, size_t len) {
    if (!buf || len == 0) {
        return;
    }

    arc4random_buf(buf, len);
}
