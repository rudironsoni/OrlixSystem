/* iXland - File Execution
 *
 * Canonical owner for exec syscalls:
 * - execve(), execv(), execvp(), execvpe()
 * - execle(), execl(), execlp()
 * - fexecve()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linux UAPI headers for ABI constants and types */
#include <linux/fcntl.h>
#include <linux/elf.h>
#include <linux/stat.h>
#include <asm-generic/stat.h>
#ifdef SIG_DFL
#undef SIG_DFL
#endif
#ifdef SIG_IGN
#undef SIG_IGN
#endif
#ifdef SIG_ERR
#undef SIG_ERR
#endif
#include <asm-generic/signal-defs.h>

#include "../kernel/task.h"

/* environ is not available on iOS; use _NSGetEnviron() */
#include <crt_externs.h>
#define environ (*_NSGetEnviron())

#include "../kernel/signal.h"
#include "../runtime/native/registry.h"
#include "fdtable.h"
#include "vfs.h"

extern int open_impl(const char *pathname, int flags, mode_t mode);
extern int close_impl(int fd);
extern ssize_t read_impl(int fd, void *buf, size_t count);

/* Forward declarations for exec variants */
int exec_native(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_elf(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_wasi(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_script(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_build_script_argv_from_line(const char *shebang_line, const char *path, int argc, char **argv,
                                      char *interpreter_path, size_t interpreter_path_len,
                                      char **script_argv, int *script_argc);

/* Deep copy argv array */
static char **exec_copy_argv(char *const argv[]) {
    if (!argv) {
        return NULL;
    }

    int argc = 0;
    while (argv[argc]) {
        argc++;
    }

    char **copy = calloc(argc + 1, sizeof(char *));
    if (!copy) {
        return NULL;
    }

    for (int i = 0; i < argc; i++) {
        copy[i] = strdup(argv[i]);
        if (!copy[i]) {
            for (int j = 0; j < i; j++) {
                free(copy[j]);
            }
            free(copy);
            return NULL;
        }
    }

    return copy;
}

/* Deep copy envp array */
static char **exec_copy_envp(char *const envp[]) {
    if (!envp) {
        return NULL;
    }

    int envc = 0;
    while (envp[envc]) {
        envc++;
    }

    char **copy = calloc(envc + 1, sizeof(char *));
    if (!copy) {
        return NULL;
    }

    for (int i = 0; i < envc; i++) {
        copy[i] = strdup(envp[i]);
        if (!copy[i]) {
            for (int j = 0; j < i; j++) {
                free(copy[j]);
            }
            free(copy);
            return NULL;
        }
    }

    return copy;
}

/* Free copied argv */
static void exec_free_argv(char **argv) {
    if (!argv) {
        return;
    }

    for (int i = 0; argv[i]; i++) {
        free(argv[i]);
    }
    free(argv);
}

/* Internal: Ensure task has an exec_image allocated */
static int exec_image_ensure(struct task_struct *task) {
    if (!task) {
        errno = EINVAL;
        return -1;
    }

    if (task->exec_image) {
        return 0;
    }

    task->exec_image = calloc(1, sizeof(struct exec_image));
    if (!task->exec_image) {
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

static void exec_record_script_image(struct task_struct *task, const char *path, const char *interpreter_path) {
    if (!task || !task->exec_image || !path || !interpreter_path) {
        return;
    }

    strncpy(task->exec_image->path, path, sizeof(task->exec_image->path) - 1);
    task->exec_image->path[sizeof(task->exec_image->path) - 1] = '\0';
    if (vfs_normalize_linux_path(interpreter_path, task->exec_image->interpreter,
                                 sizeof(task->exec_image->interpreter)) != 0) {
        task->exec_image->interpreter[0] = '\0';
    }
    task->exec_image->type = EXEC_IMAGE_SCRIPT;
}

static int exec_read_elf_header(const char *path, Elf64_Ehdr *ehdr) {
    int fd;
    ssize_t nread;
    int saved_errno;

    if (!path || !ehdr) {
        errno = EINVAL;
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    nread = read_impl(fd, ehdr, sizeof(*ehdr));
    saved_errno = errno;
    close_impl(fd);
    errno = saved_errno;

    if (nread < 0) {
        return -1;
    }
    if ((size_t)nread < sizeof(*ehdr)) {
        errno = ENOEXEC;
        return -1;
    }

    return 0;
}

static int exec_elf_header_is_magic(const Elf64_Ehdr *ehdr) {
    return ehdr &&
           ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
           ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
           ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
           ehdr->e_ident[EI_MAG3] == ELFMAG3;
}

static int exec_validate_elf64_aarch64(const Elf64_Ehdr *ehdr) {
    if (!exec_elf_header_is_magic(ehdr)) {
        errno = ENOEXEC;
        return -1;
    }
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_ident[EI_VERSION] != EV_CURRENT ||
        ehdr->e_version != EV_CURRENT ||
        ehdr->e_machine != EM_AARCH64 ||
        (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
        ehdr->e_ehsize != sizeof(Elf64_Ehdr)) {
        errno = ENOEXEC;
        return -1;
    }
    return 0;
}

static int exec_read_shebang_line(const char *path, char *buffer, size_t buffer_len) {
    char resolved_path[MAX_PATH];
    if (!path || !buffer || buffer_len < 3) {
        errno = EINVAL;
        return -1;
    }

    int ret = vfs_resolve_virtual_path_task_follow(path, resolved_path, sizeof(resolved_path), NULL, true);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    int fd = open_impl(resolved_path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    ssize_t nread = read_impl(fd, buffer, buffer_len - 1);
    int saved_errno = errno;
    close_impl(fd);
    errno = saved_errno;
    if (nread < 0) {
        return -1;
    }

    buffer[nread] = '\0';
    char *newline = strchr(buffer, '\n');
    if (newline) {
        *newline = '\0';
    }

    return 0;
}

int exec_build_script_argv(const char *path, int argc, char **argv,
                           char *interpreter_path, size_t interpreter_path_len,
                           char **script_argv, int *script_argc) {
    if (!path || !interpreter_path || interpreter_path_len == 0 || !script_argv || !script_argc) {
        errno = EINVAL;
        return -1;
    }

    char shebang[MAX_PATH];
    if (exec_read_shebang_line(path, shebang, sizeof(shebang)) < 0) {
        return -1;
    }

    return exec_build_script_argv_from_line(shebang, path, argc, argv,
                                             interpreter_path, interpreter_path_len,
                                             script_argv, script_argc);
}

int exec_build_script_argv_from_line(const char *shebang_line, const char *path, int argc, char **argv,
                                      char *interpreter_path, size_t interpreter_path_len,
                                      char **script_argv, int *script_argc) {
    if (!shebang_line || !path || !interpreter_path || interpreter_path_len == 0 || !script_argv || !script_argc) {
        errno = EINVAL;
        return -1;
    }

    if (shebang_line[0] != '#' || shebang_line[1] != '!') {
        errno = ENOEXEC;
        return -1;
    }

    char linebuf[MAX_PATH];
    strncpy(linebuf, shebang_line, sizeof(linebuf) - 1);
    linebuf[sizeof(linebuf) - 1] = '\0';

    char *cursor = linebuf + 2;
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor == '\0') {
        errno = ENOEXEC;
        return -1;
    }

    char *tokens[TASK_MAX_ARGS];
    int token_count = 0;
    while (*cursor && token_count < TASK_MAX_ARGS - 1) {
        while (*cursor && isspace((unsigned char)*cursor)) {
            *cursor = '\0';
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        tokens[token_count++] = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) {
            cursor++;
        }
    }

    if (token_count == 0) {
        errno = ENOEXEC;
        return -1;
    }

    if (strlen(tokens[0]) >= interpreter_path_len) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strcpy(interpreter_path, tokens[0]);

    int outc = 0;
    script_argv[outc++] = interpreter_path;
    for (int i = 1; i < token_count && outc < TASK_MAX_ARGS - 1; i++) {
        script_argv[outc++] = tokens[i];
    }
    script_argv[outc++] = (char *)path;
    for (int i = 1; i < argc && outc < TASK_MAX_ARGS - 1; i++) {
        script_argv[outc++] = argv[i];
    }
    script_argv[outc] = NULL;
    *script_argc = outc;

    return 0;
}

enum exec_image_type exec_classify(const char *path) {
    char resolved_path[MAX_PATH];
    int ret;

    if (!path) {
        return EXEC_IMAGE_NONE;
    }

    ret = vfs_resolve_virtual_path_task_follow(path, resolved_path, sizeof(resolved_path), NULL, true);
    if (ret != 0) {
        return EXEC_IMAGE_NONE;
    }

    if (native_lookup(resolved_path)) {
        return EXEC_IMAGE_NATIVE;
    }

    int fd = open_impl(resolved_path, O_RDONLY, 0);
    if (fd < 0) {
        return EXEC_IMAGE_NONE;
    }

    unsigned char magic[4];
    ssize_t n = read_impl(fd, magic, 4);
    close_impl(fd);

    if (n < 2) {
        return EXEC_IMAGE_NONE;
    }

    if (n >= 4 && magic[0] == ELFMAG0 && magic[1] == ELFMAG1 &&
        magic[2] == ELFMAG2 && magic[3] == ELFMAG3) {
        Elf64_Ehdr ehdr;
        if (exec_read_elf_header(resolved_path, &ehdr) != 0) {
            return EXEC_IMAGE_INVALID;
        }
        if (exec_validate_elf64_aarch64(&ehdr) != 0) {
            return EXEC_IMAGE_INVALID;
        }
        return EXEC_IMAGE_ELF;
    }

    if (n >= 4 && magic[0] == 0x00 && magic[1] == 0x61 && magic[2] == 0x73 && magic[3] == 0x6d) {
        return EXEC_IMAGE_WASI;
    }

    if (magic[0] == '#' && magic[1] == '!') {
        return EXEC_IMAGE_SCRIPT;
    }

    return EXEC_IMAGE_NONE;
}

void exec_reset_signals(struct signal_struct *sighand) {
    if (!sighand) {
        return;
    }

    for (int i = 0; i < KERNEL_SIG_NUM; i++) {
        if (sighand->actions[i].handler != SIG_IGN) {
            sighand->actions[i].handler = SIG_DFL;
        }
    }

    memset(&sighand->blocked, 0, sizeof(sighand->blocked));
    memset(&sighand->pending, 0, sizeof(sighand->pending));
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    char resolved_path[MAX_PATH];
    if (!pathname) {
        errno = EFAULT;
        return -1;
    }

    if (pathname[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    struct task_struct *task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    int ret = vfs_resolve_virtual_path_task_follow(pathname, resolved_path, sizeof(resolved_path), task->fs, true);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    int type = exec_classify(resolved_path);
    if (type == EXEC_IMAGE_NONE) {
        errno = ENOENT;
        return -1;
    }
    if (type == EXEC_IMAGE_INVALID) {
        errno = ENOEXEC;
        return -1;
    }
    if (type == EXEC_IMAGE_WASI) {
        errno = ENOEXEC;
        return -1;
    }

    char **argv_copy = exec_copy_argv(argv);
    char **envp_copy = exec_copy_envp(envp);

    if (argv && !argv_copy) {
        errno = ENOMEM;
        return -1;
    }
    if (envp && !envp_copy) {
        exec_free_argv(argv_copy);
        errno = ENOMEM;
        return -1;
    }

    if (exec_image_ensure(task) < 0) {
        exec_free_argv(argv_copy);
        exec_free_argv(envp_copy);
        return -1;
    }

    int argc = 0;
    if (argv_copy) {
        while (argv_copy[argc]) {
            argc++;
        }
    }

    int launch_status;
    if (type == EXEC_IMAGE_SCRIPT) {
        char interpreter_path[MAX_PATH];
        char *script_argv[TASK_MAX_ARGS + 4];
        int script_argc = 0;
        native_entry_fn entry;

        if (exec_build_script_argv(resolved_path, argc, argv_copy,
                                   interpreter_path, sizeof(interpreter_path),
                                   script_argv, &script_argc) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }

        entry = native_lookup(interpreter_path);
        if (!entry) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            errno = ENOENT;
            return -1;
        }

        if (task_exec_transition_impl(resolved_path, argv_copy ? argv_copy[0] : NULL) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task->signal) {
            exec_reset_signals(task->signal);
        }
        exec_record_script_image(task, resolved_path, interpreter_path);
        launch_status = entry(script_argc, script_argv, envp_copy);
    } else if (type == EXEC_IMAGE_ELF) {
        if (task_exec_transition_impl(resolved_path, argv_copy ? argv_copy[0] : NULL) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task->signal) {
            exec_reset_signals(task->signal);
        }
        launch_status = exec_elf(task, resolved_path, argc, argv_copy, envp_copy);
    } else {
        if (task_exec_transition_impl(resolved_path, argv_copy ? argv_copy[0] : NULL) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task->signal) {
            exec_reset_signals(task->signal);
        }
        launch_status = exec_native(task, resolved_path, argc, argv_copy, envp_copy);
    }

    exec_free_argv(argv_copy);
    exec_free_argv(envp_copy);

    return launch_status;
}

int execv(const char *pathname, char *const argv[]) {
    return execve(pathname, argv, environ);
}

int execvp(const char *file, char *const argv[]) {
    if (strchr(file, '/') != NULL) {
        return execv(file, argv);
    }

    const char *path_env = getenv("PATH");
    if (path_env == NULL) {
        path_env = "/usr/bin:/bin";
    }

    char *path_copy = strdup(path_env);
    if (path_copy == NULL) {
        return -1;
    }

    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);

    while (dir != NULL) {
        char fullpath[MAX_PATH];
        int len = snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, file);

        if (len > 0 && (size_t)len < sizeof(fullpath)) {
            int result = execv(fullpath, argv);
            if (result != -1) {
                free(path_copy);
                return result;
            }
            if (errno != ENOENT && errno != ENOTDIR) {
                free(path_copy);
                return -1;
            }
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
    errno = ENOENT;
    return -1;
}

int fexecve(int fd, char *const argv[], char *const envp[]) {
    char path[MAX_PATH];
    fd_entry_t *entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    int ret = get_fd_path_impl(entry, path, sizeof(path));
    int saved_errno = errno;
    put_fd_entry_impl(entry);
    errno = saved_errno;
    if (ret != 0) {
        return -1;
    }

    return execve(path, argv, envp);
}

int exec_native(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    native_entry_fn entry = native_lookup(path);
    if (!entry) {
        errno = ENOENT;
        return -1;
    }

    strncpy(task->exec_image->path, path, sizeof(task->exec_image->path) - 1);
    task->exec_image->path[sizeof(task->exec_image->path) - 1] = '\0';
    task->exec_image->type = EXEC_IMAGE_NATIVE;

    (void)task;  /* task is unused for native execution */
    return entry(argc, argv, envp);
}

int exec_elf(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    Elf64_Ehdr ehdr;
    int fd = -1;
    void *image = NULL;
    size_t image_size = 0;
    size_t image_capacity = 4096;
    size_t offset = 0;

    (void)argc;
    (void)argv;
    (void)envp;

    if (!task || !path || !task->exec_image) {
        errno = EINVAL;
        return -1;
    }

    if (exec_read_elf_header(path, &ehdr) != 0 ||
        exec_validate_elf64_aarch64(&ehdr) != 0) {
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    image = malloc(image_capacity);
    if (!image) {
        close_impl(fd);
        errno = ENOMEM;
        return -1;
    }

    for (;;) {
        ssize_t nread;

        if (offset == image_capacity) {
            size_t new_capacity = image_capacity * 2;
            void *new_image = realloc(image, new_capacity);
            if (!new_image) {
                free(image);
                close_impl(fd);
                errno = ENOMEM;
                return -1;
            }
            image = new_image;
            image_capacity = new_capacity;
        }

        nread = read_impl(fd, (char *)image + offset, image_capacity - offset);
        if (nread < 0) {
            int saved_errno = errno;
            free(image);
            close_impl(fd);
            errno = saved_errno;
            return -1;
        }
        if (nread == 0) {
            break;
        }
        offset += (size_t)nread;
    }

    close_impl(fd);
    image_size = offset;
    if (image_size < sizeof(Elf64_Ehdr)) {
        free(image);
        errno = ENOEXEC;
        return -1;
    }

    if (!task->mm) {
        task->mm = calloc(1, sizeof(*task->mm));
        if (!task->mm) {
            free(image);
            errno = ENOMEM;
            return -1;
        }
    }

    free(task->mm->exec_image_base);
    task->mm->exec_image_base = image;
    task->mm->exec_image_size = image_size;

    strncpy(task->exec_image->path, path, sizeof(task->exec_image->path) - 1);
    task->exec_image->path[sizeof(task->exec_image->path) - 1] = '\0';
    task->exec_image->interpreter[0] = '\0';
    task->exec_image->type = EXEC_IMAGE_ELF;
    task->exec_image->u.elf.entry = ehdr.e_entry;
    task->exec_image->u.elf.type = ehdr.e_type;
    task->exec_image->u.elf.machine = ehdr.e_machine;

    return 0;
}

int exec_wasi(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    (void)task;
    (void)path;
    (void)argc;
    (void)argv;
    (void)envp;
    errno = ENOEXEC;
    return -1;
}

int exec_script(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    if (!task || !path || !argv) {
        errno = EINVAL;
        return -1;
    }

    char interpreter_path[MAX_PATH];
    char *script_argv[TASK_MAX_ARGS + 4];
    int script_argc = 0;

    if (exec_build_script_argv(path, argc, argv,
                               interpreter_path, sizeof(interpreter_path),
                               script_argv, &script_argc) < 0) {
        return -1;
    }

    if (task->exec_image) {
        strncpy(task->exec_image->interpreter, interpreter_path, sizeof(task->exec_image->interpreter) - 1);
        task->exec_image->interpreter[sizeof(task->exec_image->interpreter) - 1] = '\0';
    }

    native_entry_fn entry = native_lookup(interpreter_path);
    if (!entry) {
        errno = ENOENT;
        return -1;
    }

    return entry(script_argc, script_argv, envp);
}
