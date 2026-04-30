#ifndef REGISTRY_H
#define REGISTRY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*native_entry_fn)(int argc, char **argv, char **envp);

typedef struct native_cmd {
    char *path;
    native_entry_fn entry;
    struct native_cmd *next;
} native_cmd_t;

typedef int (*registry_init_fn)(void);
typedef void (*registry_deinit_fn)(void);

typedef struct registry_entry {
    const char *name;
    registry_init_fn init;
    registry_deinit_fn deinit;
    uint32_t priority;
} registry_entry_t;

int registry_init(void);
void registry_deinit(void);

int registry_register(const char *name, registry_init_fn init, registry_deinit_fn deinit, uint32_t priority);

/* Native command registry */
void native_registry_init(void);
void native_registry_clear(void);
int native_register(const char *path, native_entry_fn entry);
native_entry_fn native_lookup(const char *path);

#ifdef __cplusplus
}
#endif

#endif
