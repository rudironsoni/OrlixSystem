#ifndef REGISTRY_H
#define REGISTRY_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*native_entry_fn)(int argc, char **argv, char **envp);
struct native_cmd;
typedef struct native_cmd native_cmd_t;
struct native_program;
typedef struct native_program native_program_t;

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
