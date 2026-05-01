/* kernel/init.h
 * Virtual kernel boot lifecycle.
 *
 * Internal kernel header - NOT public ABI.
 * Provides start_kernel / kernel_boot / kernel_is_booted vocabulary.
 */

#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

typedef __SIZE_TYPE__ size_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy API - still used by existing tests */
int library_init(const void *config);
int library_is_initialized(void);
void library_deinit(void);

/* Linux-shaped boot lifecycle */
int start_kernel(void);
int kernel_is_booted(void);
int kernel_shutdown(void);
int kernel_exec_init(const char *preferred_path, char *const argv[], char *const envp[]);

#ifdef __cplusplus
}
#endif

#endif
