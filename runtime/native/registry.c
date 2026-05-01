#include "registry.h"

#include <stdlib.h>
#include <string.h>

#include "../../fs/fdtable.h"
#include "../../fs/vfs.h"
#include "../../internal/ios/runtime/sync.h"

static native_cmd_t *registry_head = NULL;
static runtime_mutex_t registry_lock = RUNTIME_MUTEX_INITIALIZER;

void native_registry_init(void) {
    /* Registry is lazy-initialized on first register call */
}

void native_registry_clear(void) {
    runtime_mutex_lock(&registry_lock);
    native_cmd_t *cmd = registry_head;
    while (cmd) {
        native_cmd_t *next = cmd->next;
        free(cmd->path);
        free(cmd->artifact_path);
        free(cmd->abi);
        free(cmd);
        cmd = next;
    }
    registry_head = NULL;
    runtime_mutex_unlock(&registry_lock);
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

    native_cmd_t *cmd = malloc(sizeof(native_cmd_t));
    if (!cmd) {
        return -1;
    }

    cmd->path = strdup(normalized);
    if (!cmd->path) {
        free(cmd);
        return -1;
    }
    cmd->artifact_path = strdup(artifact_path);
    if (!cmd->artifact_path) {
        free(cmd->path);
        free(cmd);
        return -1;
    }
    cmd->abi = strdup(abi);
    if (!cmd->abi) {
        free(cmd->artifact_path);
        free(cmd->path);
        free(cmd);
        return -1;
    }
    cmd->entry = entry;

    runtime_mutex_lock(&registry_lock);
    cmd->next = registry_head;
    registry_head = cmd;
    runtime_mutex_unlock(&registry_lock);

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

    runtime_mutex_lock(&registry_lock);
    native_cmd_t *cmd = registry_head;
    while (cmd) {
        if (strcmp(cmd->path, normalized) == 0) {
            program->path = cmd->path;
            program->artifact_path = cmd->artifact_path;
            program->abi = cmd->abi;
            program->entry = cmd->entry;
            runtime_mutex_unlock(&registry_lock);
            return 0;
        }
        cmd = cmd->next;
    }
    runtime_mutex_unlock(&registry_lock);

    return -1;
}
