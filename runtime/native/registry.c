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
        free(cmd);
        cmd = next;
    }
    registry_head = NULL;
    runtime_mutex_unlock(&registry_lock);
}

int native_register(const char *path, native_entry_fn entry) {
    char normalized[MAX_PATH];

    if (!path || !entry) {
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
    cmd->entry = entry;

    runtime_mutex_lock(&registry_lock);
    cmd->next = registry_head;
    registry_head = cmd;
    runtime_mutex_unlock(&registry_lock);

    return 0;
}

native_entry_fn native_lookup(const char *path) {
    char normalized[MAX_PATH];

    if (!path) {
        return NULL;
    }

    if (vfs_normalize_linux_path(path, normalized, sizeof(normalized)) != 0) {
        return NULL;
    }

    runtime_mutex_lock(&registry_lock);
    native_cmd_t *cmd = registry_head;
    while (cmd) {
        if (strcmp(cmd->path, normalized) == 0) {
            native_entry_fn entry = cmd->entry;
            runtime_mutex_unlock(&registry_lock);
            return entry;
        }
        cmd = cmd->next;
    }
    runtime_mutex_unlock(&registry_lock);

    return NULL;
}
