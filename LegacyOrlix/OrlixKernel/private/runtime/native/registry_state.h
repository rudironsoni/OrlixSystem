#ifndef PRIVATE_RUNTIME_NATIVE_REGISTRY_STATE_H
#define PRIVATE_RUNTIME_NATIVE_REGISTRY_STATE_H

#include "../../../runtime/native/registry.h"

struct native_program {
    const char *path;
    const char *artifact_path;
    const char *abi;
    native_entry_fn entry;
};

struct native_cmd {
    char *path;
    char *artifact_path;
    char *abi;
    native_entry_fn entry;
    struct native_cmd *next;
};

#endif
