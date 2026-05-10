#ifndef REGISTRY_H
#define REGISTRY_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*native_entry_fn)(int argc, char **argv, char **envp);

typedef struct native_cmd {
    char *path;
    char *artifact_path;
    char *abi;
    native_entry_fn entry;
    struct native_cmd *next;
} native_cmd_t;

typedef struct native_program {
    const char *path;
    const char *artifact_path;
    const char *abi;
    native_entry_fn entry;
} native_program_t;

typedef int (*registry_init_fn)(void);
typedef void (*registry_deinit_fn)(void);

typedef struct registry_entry {
    const char *name;
    registry_init_fn init;
    registry_deinit_fn deinit;
    u32 priority;
} registry_entry_t;

int registry_init(void);
void registry_deinit(void);

int registry_register(const char *name, registry_init_fn init, registry_deinit_fn deinit, u32 priority);

/* Native command registry */
void native_registry_init(void);
void native_registry_clear(void);
int native_register(const char *path, native_entry_fn entry);
int native_register_artifact(const char *path, const char *artifact_path, const char *abi, native_entry_fn entry);
native_entry_fn native_lookup(const char *path);
int native_lookup_program(const char *path, native_program_t *program);

#ifdef __cplusplus
}
#endif

#endif
