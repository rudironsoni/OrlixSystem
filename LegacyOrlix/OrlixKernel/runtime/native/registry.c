#include "registry.h"

#include "../../private/runtime/native/registry_state.h"
#include "../../private/kernel/mutex_state.h"
#include "../../fs/path.h"
#include "../../fs/vfs.h"

#include <linux/gfp_types.h>
#include <linux/string.h>

extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);

static native_cmd_t *registry_head = NULL;
static kernel_mutex_t registry_lock = KERNEL_MUTEX_INITIALIZER;

void native_registry_init(void) {
    /* Registry is lazy-initialized on first register call */
}

void native_registry_clear(void) {
    kernel_mutex_lock(&registry_lock);
    native_cmd_t *cmd = registry_head;
    while (cmd) {
        native_cmd_t *next = cmd->next;
        kfree(cmd->path);
        kfree(cmd->artifact_path);
        kfree(cmd->abi);
        kfree(cmd);
        cmd = next;
    }
    registry_head = NULL;
    kernel_mutex_unlock(&registry_lock);
}

int native_register(const char *path, native_entry_fn entry) {
    return native_register_artifact(path, path, "native-ios-arm64", entry);
}

int native_register_artifact(const char *path, const char *artifact_path, const char *abi, native_entry_fn entry) {
    char normalized[MAX_PATH];

    if (!path || !artifact_path || !abi || !entry) {
        return -1;
    }

    if (vfs_normalize_linux_path(path, normalized, sizeof(normalized)) != 0) {
        return -1;
    }

    native_cmd_t *cmd = __kmalloc_noprof(sizeof(native_cmd_t), GFP_KERNEL);
    if (!cmd) {
        return -1;
    }

    cmd->path = kstrdup(normalized, GFP_KERNEL);
    if (!cmd->path) {
        kfree(cmd);
        return -1;
    }
    cmd->artifact_path = kstrdup(artifact_path, GFP_KERNEL);
    if (!cmd->artifact_path) {
        kfree(cmd->path);
        kfree(cmd);
        return -1;
    }
    cmd->abi = kstrdup(abi, GFP_KERNEL);
    if (!cmd->abi) {
        kfree(cmd->artifact_path);
        kfree(cmd->path);
        kfree(cmd);
        return -1;
    }
    cmd->entry = entry;

    kernel_mutex_lock(&registry_lock);
    cmd->next = registry_head;
    registry_head = cmd;
    kernel_mutex_unlock(&registry_lock);

    return 0;
}

native_entry_fn native_lookup(const char *path) {
    native_program_t program;

    if (native_lookup_program(path, &program) != 0) {
        return NULL;
    }
    return program.entry;
}

int native_lookup_program(const char *path, native_program_t *program) {
    char normalized[MAX_PATH];

    if (!path || !program) {
        return -1;
    }

    if (vfs_normalize_linux_path(path, normalized, sizeof(normalized)) != 0) {
        return -1;
    }

    kernel_mutex_lock(&registry_lock);
    native_cmd_t *cmd = registry_head;
    while (cmd) {
        if (strcmp(cmd->path, normalized) == 0) {
            program->path = cmd->path;
            program->artifact_path = cmd->artifact_path;
            program->abi = cmd->abi;
            program->entry = cmd->entry;
            kernel_mutex_unlock(&registry_lock);
            return 0;
        }
        cmd = cmd->next;
    }
    kernel_mutex_unlock(&registry_lock);

    return -1;
}
