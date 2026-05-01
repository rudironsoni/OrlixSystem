#include <linux/fcntl.h>
#include <linux/elf.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "kernel/task.h"
#include "runtime/native/registry.h"

extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int fexecve(int fd, char *const argv[], char *const envp[]);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern long write_impl(int fd, const void *buf, size_t count);
extern long read_impl(int fd, void *buf, size_t count);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int unlink_impl(const char *pathname);
extern int symlinkat(const char *target, int newdirfd, const char *linkpath);

static int close_if_open(int fd) {
    if (fd >= 0 && fdtable_is_used_impl(fd)) {
        return close_impl(fd);
    }
    return 0;
}

static int expect_errno(int expected) {
    if (errno != expected) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int read_proc_file(const char *path, char *buf, size_t buf_len, ssize_t *out_len) {
    int fd;
    ssize_t nread;

    if (!path || !buf || buf_len == 0 || !out_len) {
        errno = EINVAL;
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, buf_len);
    close_impl(fd);
    if (nread < 0) {
        return -1;
    }
    *out_len = nread;
    return 0;
}

static int expect_nul_vector(const char *buf, ssize_t len, const char *const expected[]) {
    size_t pos = 0;

    if (!buf || len < 0 || !expected) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; expected[i]; i++) {
        size_t item_len = strlen(expected[i]);
        if (pos + item_len + 1 > (size_t)len) {
            errno = EPROTO;
            return -1;
        }
        if (memcmp(buf + pos, expected[i], item_len) != 0 || buf[pos + item_len] != '\0') {
            errno = EPROTO;
            return -1;
        }
        pos += item_len + 1;
    }

    if (pos != (size_t)len) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int native_exec_status(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    return 23;
}

static int captured_argc;
static char captured_argv[8][MAX_PATH];
static char captured_env0[MAX_PATH];

static void clear_captured_exec(void) {
    captured_argc = 0;
    memset(captured_argv, 0, sizeof(captured_argv));
    memset(captured_env0, 0, sizeof(captured_env0));
}

static int native_capture_exec(int argc, char **argv, char **envp) {
    captured_argc = argc;
    for (int i = 0; i < argc && i < 8; i++) {
        if (argv && argv[i]) {
            size_t len = strlen(argv[i]);
            if (len >= sizeof(captured_argv[i])) {
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(captured_argv[i], argv[i], len + 1);
        }
    }
    if (envp && envp[0]) {
        size_t len = strlen(envp[0]);
        if (len >= sizeof(captured_env0)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(captured_env0, envp[0], len + 1);
    }
    return 37;
}

static int create_exec_file(const char *path, const char *content) {
    int fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return -1;
    }
    if (content) {
        size_t len = strlen(content);
        if (write_impl(fd, content, len) != (long)len) {
            int saved_errno = errno;
            close_impl(fd);
            errno = saved_errno;
            return -1;
        }
    }
    return close_impl(fd);
}

static int create_exec_bytes(const char *path, const void *content, size_t len) {
    int fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return -1;
    }
    if (len > 0 && write_impl(fd, content, len) != (long)len) {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
        return -1;
    }
    return close_impl(fd);
}

static void build_minimal_elf64_aarch64(Elf64_Ehdr *ehdr) {
    memset(ehdr, 0, sizeof(*ehdr));
    ehdr->e_ident[EI_MAG0] = ELFMAG0;
    ehdr->e_ident[EI_MAG1] = ELFMAG1;
    ehdr->e_ident[EI_MAG2] = ELFMAG2;
    ehdr->e_ident[EI_MAG3] = ELFMAG3;
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_ident[EI_VERSION] = EV_CURRENT;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_AARCH64;
    ehdr->e_version = EV_CURRENT;
    ehdr->e_entry = 0x400000;
    ehdr->e_ehsize = sizeof(*ehdr);
}

static void build_loadable_elf64_aarch64(unsigned char *image, size_t image_len) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    Elf64_Phdr *interp;
    const char interp_path[] = "/lib/ld-linux-aarch64.so.1";
    size_t interp_offset = sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr));
    size_t text_offset = interp_offset + sizeof(interp_path);

    memset(image, 0, image_len);
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 2;

    interp = (Elf64_Phdr *)(image + ehdr->e_phoff);
    interp->p_type = PT_INTERP;
    interp->p_offset = interp_offset;
    interp->p_filesz = sizeof(interp_path);
    interp->p_memsz = sizeof(interp_path);
    memcpy(image + interp_offset, interp_path, sizeof(interp_path));

    load = interp + 1;
    load->p_type = PT_LOAD;
    load->p_flags = PF_R | PF_X;
    load->p_offset = text_offset;
    load->p_vaddr = 0x400000;
    load->p_filesz = image_len - text_offset;
    load->p_memsz = load->p_filesz + 16;
    load->p_align = 0x1000;
    image[text_offset] = 0xaa;
    image[text_offset + 1] = 0xbb;
}

static int verify_state_unchanged(struct task_struct *task,
                                  const char *expected_exe,
                                  const char *expected_comm,
                                  bool expected_execed,
                                  int cloexec_fd,
                                  int keep_fd) {
    if (strcmp(task->exe, expected_exe) != 0) {
        errno = EPROTO;
        return -1;
    }
    if (strcmp(task->comm, expected_comm) != 0) {
        errno = EPROTO;
        return -1;
    }
    if (atomic_load(&task->execed) != expected_execed) {
        errno = EPROTO;
        return -1;
    }
    if ((cloexec_fd >= 0 && !fdtable_is_used_impl(cloexec_fd)) ||
        (keep_fd >= 0 && !fdtable_is_used_impl(keep_fd))) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int exec_syscall_contract_rejects_null_path_without_transition(void) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve(NULL, NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (expect_errno(EFAULT) != 0) {
        return -1;
    }
    return verify_state_unchanged(task, "/before", "before", false, -1, -1);
}

int exec_syscall_contract_rejects_empty_path_without_transition(void) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("", NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (expect_errno(ENOENT) != 0) {
        return -1;
    }
    return verify_state_unchanged(task, "/before", "before", false, -1, -1);
}

int exec_syscall_contract_missing_path_preserves_state_and_cloexec_fds(void) {
    struct task_struct *task = get_current();
    int cloexec_fd = -1;
    int keep_fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (cloexec_fd < 0) {
        return -1;
    }

    keep_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (keep_fd < 0) {
        goto out;
    }

    errno = 0;
    if (execve("/missing", NULL, NULL) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (expect_errno(ENOENT) != 0) {
        goto out;
    }
    if (verify_state_unchanged(task, "/before", "before", false, cloexec_fd, keep_fd) != 0) {
        goto out;
    }

    result = 0;

out:
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int exec_syscall_contract_native_success_applies_transition_and_returns_entry_status(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"custom-shell", "arg1", NULL};
    char *envp[] = {"A=B", NULL};
    int cloexec_fd = -1;
    int keep_fd = -1;
    char link_target[MAX_PATH];
    long link_len;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    if (native_register("//usr//bin///env/", native_exec_status) != 0) {
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (cloexec_fd < 0) {
        goto out;
    }

    keep_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (keep_fd < 0) {
        goto out;
    }

    status = execve("//usr//bin///env/", argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed)) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->exe, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->comm, "custom-shell") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (fdtable_is_used_impl(cloexec_fd) || !fdtable_is_used_impl(keep_fd)) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->exec_image->path, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }

    link_len = readlink_impl("/proc/self/exe", link_target, sizeof(link_target));
    if (link_len < 0) {
        goto out;
    }
    if ((size_t)link_len >= sizeof(link_target)) {
        errno = EPROTO;
        goto out;
    }
    link_target[link_len] = '\0';
    if (strcmp(link_target, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int exec_syscall_contract_native_exec_records_proc_cmdline_and_environ(void) {
    char *argv[] = {"custom-shell", "arg1", "arg two", NULL};
    char *envp[] = {"A=B", "C=D", NULL};
    const char *const expected_cmdline[] = {"custom-shell", "arg1", "arg two", NULL};
    const char *const expected_environ[] = {"A=B", "C=D", NULL};
    char buf[512];
    ssize_t nread = 0;
    int status;
    int result = -1;

    native_registry_clear();
    if (native_register("/usr/bin/proc-env", native_exec_status) != 0) {
        return -1;
    }

    status = execve("/usr/bin/proc-env", argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }

    if (read_proc_file("/proc/self/cmdline", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        errno = ENOMSG;
        goto out;
    }

    if (read_proc_file("/proc/self/environ", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_environ) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    return result;
}

int exec_syscall_contract_oversized_argv_returns_e2big_without_transition(void) {
    struct task_struct *task = get_current();
    char *too_many[TASK_MAX_ARGS + 1];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    if (native_register("/usr/bin/too-many-args", native_exec_status) != 0) {
        return -1;
    }
    for (int i = 0; i < TASK_MAX_ARGS; i++) {
        too_many[i] = "x";
    }
    too_many[TASK_MAX_ARGS] = NULL;

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/usr/bin/too-many-args", too_many, NULL) != -1 ||
        expect_errno(E2BIG) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    return result;
}

int exec_syscall_contract_script_uses_virtual_path_and_native_interpreter(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"script-name", "arg1", NULL};
    char *envp[] = {"KEY=VALUE", NULL};
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-script-launch");

    if (create_exec_file("/tmp/exec-script-launch", "#!//usr//bin///interp/\n") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("//tmp///exec-script-launch", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed) ||
        strcmp(task->exe, "/tmp/exec-script-launch") != 0 ||
        strcmp(task->comm, "script-name") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->path, "/tmp/exec-script-launch") != 0 ||
        strcmp(task->exec_image->interpreter, "/usr/bin/interp") != 0 ||
        task->exec_image->type != EXEC_IMAGE_SCRIPT) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 3 ||
        strcmp(captured_argv[0], "//usr//bin///interp/") != 0 ||
        strcmp(captured_argv[1], "/tmp/exec-script-launch") != 0 ||
        strcmp(captured_argv[2], "arg1") != 0 ||
        strcmp(captured_env0, "KEY=VALUE") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-script-launch");
    return result;
}

int exec_syscall_contract_script_exec_records_interpreter_proc_cmdline(void) {
    char *argv[] = {"script-name", "arg1", NULL};
    char *envp[] = {"KEY=VALUE", NULL};
    const char *const expected_cmdline[] = {"/usr/bin/interp", "-x", "/tmp/exec-script-proc", "arg1", NULL};
    const char *const expected_environ[] = {"KEY=VALUE", NULL};
    char buf[512];
    ssize_t nread = 0;
    int status;
    int result = -1;

    native_registry_clear();
    unlink_impl("/tmp/exec-script-proc");

    if (create_exec_file("/tmp/exec-script-proc", "#!/usr/bin/interp -x\n") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("/tmp/exec-script-proc", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }

    if (read_proc_file("/proc/self/cmdline", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        errno = ENOMSG;
        goto out;
    }

    if (read_proc_file("/proc/self/environ", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_environ) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-script-proc");
    return result;
}

int exec_syscall_contract_script_symlink_records_resolved_target(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"script-link", "arg1", NULL};
    char *envp[] = {"LINK=1", NULL};
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-script-link");
    unlink_impl("/tmp/exec-script-real");

    if (create_exec_file("/tmp/exec-script-real", "#!/usr/bin/interp\n") != 0) {
        goto out;
    }
    if (symlinkat("/tmp/exec-script-real", AT_FDCWD, "/tmp/exec-script-link") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("/tmp/exec-script-link", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed) ||
        strcmp(task->exe, "/tmp/exec-script-real") != 0 ||
        strcmp(task->comm, "script-link") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->path, "/tmp/exec-script-real") != 0 ||
        strcmp(task->exec_image->interpreter, "/usr/bin/interp") != 0 ||
        task->exec_image->type != EXEC_IMAGE_SCRIPT) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 3 ||
        strcmp(captured_argv[0], "/usr/bin/interp") != 0 ||
        strcmp(captured_argv[1], "/tmp/exec-script-real") != 0 ||
        strcmp(captured_argv[2], "arg1") != 0 ||
        strcmp(captured_env0, "LINK=1") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-script-link");
    unlink_impl("/tmp/exec-script-real");
    return result;
}

int exec_syscall_contract_missing_script_interpreter_preserves_state(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"script-name", NULL};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-script-missing-interpreter");
    if (create_exec_file("/tmp/exec-script-missing-interpreter", "#!/usr/bin/not-there\n") != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/tmp/exec-script-missing-interpreter", argv, NULL) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (expect_errno(ENOENT) != 0) {
        goto out;
    }
    if (verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-script-missing-interpreter");
    return result;
}

int exec_syscall_contract_fexecve_uses_fd_path(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"fd-native", NULL};
    char *envp[] = {"FD=1", NULL};
    int fd = -1;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-native-fd");
    if (create_exec_file("/tmp/exec-native-fd", "") != 0) {
        goto out;
    }
    fd = open_impl("//tmp///exec-native-fd", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (native_register("/tmp/exec-native-fd", native_capture_exec) != 0) {
        goto out;
    }

    status = fexecve(fd, argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed) ||
        strcmp(task->exe, "/tmp/exec-native-fd") != 0 ||
        strcmp(task->comm, "fd-native") != 0 ||
        captured_argc != 1 ||
        strcmp(captured_argv[0], "fd-native") != 0 ||
        strcmp(captured_env0, "FD=1") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(fd);
    unlink_impl("/tmp/exec-native-fd");
    return result;
}

int exec_syscall_contract_fexecve_rejects_invalid_fd(void) {
    errno = 0;
    if (fexecve(240, NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(EBADF);
}

int exec_syscall_contract_elf64_aarch64_exec_loads_virtual_image(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"elf-prog", "arg1", NULL};
    char *envp[] = {"ELF=1", NULL};
    const char *const expected_cmdline[] = {"elf-prog", "arg1", NULL};
    const char *const expected_environ[] = {"ELF=1", NULL};
    unsigned char image[sizeof(Elf64_Ehdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    char buf[256];
    ssize_t nread = 0;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf64");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    image[sizeof(Elf64_Ehdr)] = 0xaa;
    image[sizeof(Elf64_Ehdr) + 1] = 0xbb;
    if (create_exec_bytes("/tmp/exec-elf64", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    status = execve("/tmp/exec-elf64", argv, envp);
    if (status != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed) ||
        strcmp(task->exe, "/tmp/exec-elf64") != 0 ||
        strcmp(task->comm, "elf-prog") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        task->exec_image->type != EXEC_IMAGE_ELF ||
        strcmp(task->exec_image->path, "/tmp/exec-elf64") != 0 ||
        task->exec_image->u.elf.entry != ehdr->e_entry ||
        task->exec_image->u.elf.machine != EM_AARCH64) {
        errno = EPROTO;
        goto out;
    }
    if (!task->mm ||
        !task->mm->exec_image_base ||
        task->mm->exec_image_size != sizeof(image) ||
        memcmp(task->mm->exec_image_base, image, sizeof(image)) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (read_proc_file("/proc/self/cmdline", buf, sizeof(buf), &nread) != 0 ||
        expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        goto out;
    }
    if (read_proc_file("/proc/self/environ", buf, sizeof(buf), &nread) != 0 ||
        expect_nul_vector(buf, nread, expected_environ) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf64");
    return result;
}

int exec_syscall_contract_elf_program_headers_create_virtual_segments(void) {
    struct task_struct *task = get_current();
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-loadable");
    build_loadable_elf64_aarch64(image, sizeof(image));
    load = ((Elf64_Phdr *)(image + ehdr->e_phoff)) + 1;
    if (create_exec_bytes("/tmp/exec-elf-loadable", image, sizeof(image)) != 0) {
        goto out;
    }

    status = execve("/tmp/exec-elf-loadable", NULL, NULL);
    if (status != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->mm ||
        task->mm->exec_entry != ehdr->e_entry ||
        task->mm->exec_segment_count != 1 ||
        task->mm->exec_segments[0].vaddr != load->p_vaddr ||
        task->mm->exec_segments[0].filesz != load->p_filesz ||
        task->mm->exec_segments[0].memsz != load->p_memsz ||
        task->mm->exec_segments[0].offset != load->p_offset ||
        task->mm->exec_segments[0].flags != load->p_flags) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->interpreter, "/lib/ld-linux-aarch64.so.1") != 0 ||
        task->exec_image->u.elf.type != ET_EXEC) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-loadable");
    return result;
}

int exec_syscall_contract_elf_dyn_image_is_accepted(void) {
    struct task_struct *task = get_current();
    unsigned char image[sizeof(Elf64_Ehdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-dyn");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_type = ET_DYN;
    if (create_exec_bytes("/tmp/exec-elf-dyn", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-dyn", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        task->exec_image->type != EXEC_IMAGE_ELF ||
        task->exec_image->u.elf.type != ET_DYN ||
        strcmp(task->exec_image->path, "/tmp/exec-elf-dyn") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-dyn");
    return result;
}

int exec_syscall_contract_elf_interp_without_nul_returns_enoexec_without_transition(void) {
    struct task_struct *task = get_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *interp;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-bad-interp");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;
    interp = (Elf64_Phdr *)(image + ehdr->e_phoff);
    interp->p_type = PT_INTERP;
    interp->p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    interp->p_filesz = 4;
    interp->p_memsz = 4;
    memcpy(image + interp->p_offset, "/bad", 4);
    if (create_exec_bytes("/tmp/exec-elf-bad-interp", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/tmp/exec-elf-bad-interp", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-bad-interp");
    return result;
}

int exec_syscall_contract_elf_too_many_load_segments_returns_enoexec_without_transition(void) {
    struct task_struct *task = get_current();
    unsigned char image[sizeof(Elf64_Ehdr) + ((TASK_EXEC_MAX_LOAD_SEGMENTS + 1) * sizeof(Elf64_Phdr)) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-too-many-loads");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = TASK_EXEC_MAX_LOAD_SEGMENTS + 1;
    for (int i = 0; i < TASK_EXEC_MAX_LOAD_SEGMENTS + 1; i++) {
        Elf64_Phdr *load = ((Elf64_Phdr *)(image + ehdr->e_phoff)) + i;
        load->p_type = PT_LOAD;
        load->p_offset = sizeof(image) - 8;
        load->p_filesz = 1;
        load->p_memsz = 1;
        load->p_vaddr = 0x400000 + ((uint64_t)i * 0x1000);
        load->p_flags = PF_R;
    }
    if (create_exec_bytes("/tmp/exec-elf-too-many-loads", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/tmp/exec-elf-too-many-loads", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-too-many-loads");
    return result;
}

int exec_syscall_contract_elf_bad_load_segment_returns_enoexec_without_transition(void) {
    struct task_struct *task = get_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-bad-segment");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_type = PT_LOAD;
    load->p_offset = sizeof(image) - 4;
    load->p_filesz = 8;
    load->p_memsz = 8;
    load->p_flags = PF_R;
    if (create_exec_bytes("/tmp/exec-elf-bad-segment", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/tmp/exec-elf-bad-segment", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-bad-segment");
    return result;
}

int exec_syscall_contract_elf_wrong_machine_returns_enoexec_without_transition(void) {
    struct task_struct *task = get_current();
    Elf64_Ehdr ehdr;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-wrong-machine");
    build_minimal_elf64_aarch64(&ehdr);
    ehdr.e_machine = EM_386;
    if (create_exec_bytes("/tmp/exec-elf-wrong-machine", &ehdr, sizeof(ehdr)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/tmp/exec-elf-wrong-machine", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-wrong-machine");
    return result;
}

int exec_syscall_contract_truncated_elf_returns_enoexec_without_transition(void) {
    struct task_struct *task = get_current();
    unsigned char magic[4] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-truncated");
    if (create_exec_bytes("/tmp/exec-elf-truncated", magic, sizeof(magic)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/tmp/exec-elf-truncated", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-truncated");
    return result;
}
