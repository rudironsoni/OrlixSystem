/* LocalKernelPrototype/OrlixHostAdapterTests/HostTestSupport.h
 * Host-side test support helpers
 *
 * This header declares host-side helpers needed for test setup.
 * These are NOT Linux UAPI proof - they are Darwin/host operations.
 *
 * This header uses Darwin headers only.
 */

#ifndef HOST_TEST_SUPPORT_H
#define HOST_TEST_SUPPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Signal test helpers (Darwin implementation)
 * ============================================================================ */

/* Install SIG_IGN handler for SIGINT, returns 0 on success, -1 on error */
int host_test_signal_install_sigint_ign(void);

/* Restore SIGINT handler to previous disposition, returns 0 on success */
int host_test_signal_restore_sigint(void);

/* Block SIGINT, returns 0 on success, -1 on error */
int host_test_signal_block_sigint(void);

/* Restore signal mask from previous blocked state, returns 0 on success */
int host_test_signal_restore_mask(void);

/* ============================================================================
 * fcntl semantic test helpers (Darwin implementation)
 * ============================================================================ */

/* Duplicate fd to min_fd or higher, returns new fd or -1 on error */
int host_test_fcntl_dupfd(int fd, int min_fd);

/* Duplicate fd with FD_CLOEXEC set, returns new fd or -1 on error */
int host_test_fcntl_dupfd_cloexec(int fd, int min_fd);

/* Get fd flags, returns flags or -1 on error */
int host_test_fcntl_getfd(int fd);

/* Set fd flags, returns 0 on success, -1 on error */
int host_test_fcntl_setfd(int fd, int flags);

/* Get file status flags, returns flags or -1 on error */
int host_test_fcntl_getfl(int fd);

/* Returns non-zero if FD_CLOEXEC is set in flags */
int host_test_fcntl_has_cloexec(int flags);

/* Returns non-zero if O_RDONLY is set in flags */
int host_test_fcntl_has_rdonly(int flags);

/* Open a host path read-only through the backing bridge, returns fd or -1 */
int host_test_backing_open_readonly(const char *path);

/* Close a host fd through the backing bridge, returns 0 on success or -1 */
int host_test_backing_close(int fd);

#ifdef __cplusplus
}
#endif

#endif /* HOST_TEST_SUPPORT_H */
