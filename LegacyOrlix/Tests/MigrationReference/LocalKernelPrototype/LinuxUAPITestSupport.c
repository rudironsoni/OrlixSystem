/* LocalKernelPrototype/LinuxUAPITestSupport.c
 * Semantic test helpers for Linux UAPI-sensitive assertions
 *
 * This file implements semantic helpers that interpret Linux UAPI values.
 * Tests use these for behavior assertions, not constant accessors.
 *
 * ALLOWED: Semantic interpretation helpers (e.g., is_directory(mode))
 * FORBIDDEN: Constant accessor soup (e.g., sigusr1(), at_nofollow())
 */

#include <linux/stat.h>

/* ============================================================================
 * Stat mode semantic helpers - interpret mode values
 * ============================================================================ */

int stat_mode_is_directory(unsigned int mode) {
    return (mode & S_IFMT) == S_IFDIR;
}

int stat_mode_is_symlink(unsigned int mode) {
    return (mode & S_IFMT) == S_IFLNK;
}

int stat_mode_is_regular(unsigned int mode) {
    return (mode & S_IFMT) == S_IFREG;
}

int stat_mode_is_char_device(unsigned int mode) {
    return (mode & S_IFMT) == S_IFCHR;
}

int stat_mode_is_block_device(unsigned int mode) {
    return (mode & S_IFMT) == S_IFBLK;
}

int stat_mode_is_fifo(unsigned int mode) {
    return (mode & S_IFMT) == S_IFIFO;
}
