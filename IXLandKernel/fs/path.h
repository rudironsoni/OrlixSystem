#ifndef PATH_H
#define PATH_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_PATH 4096

#ifdef __cplusplus
extern "C" {
#endif

/* Path classification types */
typedef enum {
    PATH_INVALID = 0,
    PATH_OWN_SANDBOX,
    PATH_VIRTUAL_LINUX,
    PATH_ABSOLUTE_HOST,
    PATH_EXTERNAL
} path_type_t;

/* Path classification and translation */
path_type_t path_classify(const char *path);
bool path_is_virtual_linux(const char *path);
bool path_is_own_sandbox(const char *path);
bool path_is_external(const char *path);
bool path_is_direct(const char *path);

/* Path normalization */
void path_normalize(char *path);
int path_normalize_with_len(char *path, size_t path_len);

/* Path translation between virtual and host */
int path_translate(const char *virtual_path, char *host_path, size_t host_path_len);
int path_reverse_translate(const char *host_path, char *virtual_path, size_t virtual_path_len);
int path_virtual_to_ios(const char *vpath, char *ios_path, size_t ios_path_len);

/* Path resolution */
int path_resolve(const char *path, char *resolved, size_t resolved_len);
void path_join(const char *base, const char *rel, char *result, size_t result_len);

/* Path validation */
bool path_is_valid(const char *path);
bool path_is_safe(const char *path);
bool path_in_sandbox(const char *path);

/* Path subsystem lifecycle */
int path_init(void);
void path_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
