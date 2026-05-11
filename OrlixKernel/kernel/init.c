/* iOS Subsystem for Linux - Library Initialization
 *
 * Automatic initialization using constructor attribute
 * AND explicit Linux-shaped boot lifecycle (start_kernel/kernel_shutdown)
 */

#include <linux/errno.h>
#include <linux/atomic.h>

#include "../fs/fdtable.h"
#include "../fs/vfs.h"
#include "../runtime/native/registry.h"
#include "task.h"

extern int execve_impl(const char *pathname, char *const argv[], char *const envp[]);

/* Global initialization state */
static atomic_t library_initialized = ATOMIC_INIT(0);
static atomic_t kernel_booted = ATOMIC_INIT(0);
static kernel_mutex_t library_init_lock = KERNEL_MUTEX_INITIALIZER;

/* ============================================================================
 * INTERNAL INITIALIZATION - lock must be held by caller
 * ============================================================================ */

static int kernel_init_locked(void) {
    int vfs_result;
    int task_result;

    if (atomic_read(&library_initialized)) {
        return 0; /* Already initialized */
    }

    /* Initialize VFS first */
    vfs_result = vfs_init();
    if (vfs_result != 0) {
        return -1; /* VFS init failed */
    }

    /* Initialize task system - creates init task */
    task_result = task_init();
    if (task_result != 0) {
        vfs_deinit();
        return -1; /* Task init failed */
    }

    /* Initialize native command registry */
    native_registry_init();

    atomic_set(&library_initialized, 1);
    return 0;
}

static void kernel_deinit_locked(void) {
    if (!atomic_read(&library_initialized)) {
        return;
    }

    task_deinit();
    file_deinit_impl();
    vfs_deinit();
    atomic_set(&library_initialized, 0);
    atomic_set(&kernel_booted, 0);
}

/* ============================================================================
 * CONSTRUCTOR/DESTRUCTOR - for auto-init on library load
 * ============================================================================ */

void library_init_constructor(void) __attribute__((constructor(101))) __attribute__((used));
void library_deinit_destructor(void) __attribute__((destructor)) __attribute__((used));

void library_init_constructor(void) {
    /* Fast path: check without lock */
    if (atomic_read(&library_initialized)) {
        return;
    }

    kernel_mutex_lock(&library_init_lock);

    /* Double-check after acquiring lock */
    if (atomic_read(&library_initialized)) {
        kernel_mutex_unlock(&library_init_lock);
        return;
    }

    kernel_init_locked();
    atomic_set(&kernel_booted, 1); /* Constructor counts as booted */

    kernel_mutex_unlock(&library_init_lock);
}

void library_deinit_destructor(void) {
    kernel_mutex_lock(&library_init_lock);
    kernel_deinit_locked();
    kernel_mutex_unlock(&library_init_lock);
}

/* ============================================================================
 * LEGACY PUBLIC INITIALIZATION API
 * ============================================================================ */

int library_init(const void *config) {
    int result;
    (void)config;

    if (atomic_read(&library_initialized)) {
        return 0;
    }

    kernel_mutex_lock(&library_init_lock);
    result = kernel_init_locked();
    kernel_mutex_unlock(&library_init_lock);

    return result;
}

int library_is_initialized(void) {
    return atomic_read(&library_initialized);
}

const char *library_version(void) {
    return "1.0.0";
}

void library_deinit(void) {
    kernel_mutex_lock(&library_init_lock);
    kernel_deinit_locked();
    kernel_mutex_unlock(&library_init_lock);
}

/* ============================================================================
 * LINUX-SHAPED BOOT LIFECYCLE
 * ============================================================================ */

int start_kernel(void) {
    int result;

    /* Fast path: already booted */
    if (atomic_read(&kernel_booted) && atomic_read(&library_initialized)) {
        /* Ensure the calling host thread has a current task bound. The kernel
         * boot state is global, but current-task binding is per host thread. */
        (void)task_init();
        return 0;
    }

    kernel_mutex_lock(&library_init_lock);

    /* Double-check after acquiring lock */
    if (atomic_read(&kernel_booted) && atomic_read(&library_initialized)) {
        kernel_mutex_unlock(&library_init_lock);
        return 0;
    }

    /* Initialize subsystems */
    result = kernel_init_locked();
    if (result != 0) {
        kernel_mutex_unlock(&library_init_lock);
        return result;
    }

    atomic_set(&kernel_booted, 1);
    kernel_mutex_unlock(&library_init_lock);
    return 0;
}

int kernel_is_booted(void) {
    return atomic_read(&kernel_booted) && atomic_read(&library_initialized);
}

int kernel_shutdown(void) {
    kernel_mutex_lock(&library_init_lock);

    if (!atomic_read(&kernel_booted) && !atomic_read(&library_initialized)) {
        kernel_mutex_unlock(&library_init_lock);
        return 0; /* Already shut down */
    }

    kernel_deinit_locked();

    kernel_mutex_unlock(&library_init_lock);
    return 0;
}

static int kernel_try_exec_init_path(const char *path, char *const argv[], char *const envp[]) {
    char *default_argv[] = {(char *)path, NULL};
    char *default_envp[] = {
        "HOME=/",
        "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
        NULL,
    };

    if (!path || path[0] == '\0') {
        return -ENOENT;
    }

    return execve_impl(path, argv ? argv : default_argv, envp ? envp : default_envp);
}

int kernel_exec_init(const char *preferred_path, char *const argv[], char *const envp[]) {
    static const char *const init_candidates[] = {
        "/sbin/init",
        "/etc/init",
        "/bin/init",
        "/bin/sh",
    };
    int saved_error = -ENOENT;

    if (!kernel_is_booted() && start_kernel() != 0) {
        return -EINVAL;
    }

    if (preferred_path) {
        return kernel_try_exec_init_path(preferred_path, argv, envp);
    }

    for (size_t i = 0; i < sizeof(init_candidates) / sizeof(init_candidates[0]); i++) {
        int ret = kernel_try_exec_init_path(init_candidates[i], argv, envp);
        if (ret == 0) {
            return 0;
        }
        saved_error = ret;
    }

    return saved_error;
}
