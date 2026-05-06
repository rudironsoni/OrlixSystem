/* IXLandSystemTests/LinuxUAPITestSupport.h
 * Semantic test helpers for Linux UAPI-sensitive assertions
 *
 * This header declares semantic helpers that interpret Linux UAPI values.
 * Tests use these for behavior assertions, not constant accessors.
 *
 * ALLOWED: Semantic interpretation helpers (e.g., is_directory(mode))
 * FORBIDDEN: Constant accessor soup (e.g., sigusr1(), at_nofollow())
 * FORBIDDEN: Linux UAPI includes - these are scoped to LinuxUAPITestSupport.c only
 */

#ifndef LINUX_UAPI_TEST_SUPPORT_H
#define LINUX_UAPI_TEST_SUPPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Stat mode semantic helpers - interpret mode values
 * ============================================================================ */

/* Returns non-zero if mode represents a directory */
int stat_mode_is_directory(unsigned int mode);

/* Returns non-zero if mode represents a symlink */
int stat_mode_is_symlink(unsigned int mode);

/* Returns non-zero if mode represents a regular file */
int stat_mode_is_regular(unsigned int mode);

/* Returns non-zero if mode represents a character device */
int stat_mode_is_char_device(unsigned int mode);

/* Returns non-zero if mode represents a block device */
int stat_mode_is_block_device(unsigned int mode);

/* Returns non-zero if mode represents a FIFO */
int stat_mode_is_fifo(unsigned int mode);

#ifdef __cplusplus
}
#endif

#endif /* LINUX_UAPI_TEST_SUPPORT_H */
