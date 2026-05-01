/* IXLandSystem/kernel/uts.h
 * Private internal header for virtual UTS namespace state.
 *
 * This is runtime-owned namespace state, not a Linux UAPI mirror.
 */

#ifndef KERNEL_UTS_H
#define KERNEL_UTS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct new_utsname;
struct uts_namespace;

struct uts_namespace *uts_get_initial_namespace(void);
struct uts_namespace *uts_get(struct uts_namespace *ns);
void uts_put(struct uts_namespace *ns);
struct uts_namespace *uts_dup(struct uts_namespace *ns);
int uts_unshare_current(void);
void uts_reset_initial_namespace(void);
void uts_reset_current_namespace(void);

int uname_impl(struct new_utsname *buf);
int gethostname_impl(char *name, size_t len);
int sethostname_impl(const char *name, size_t len);
int getdomainname_impl(char *name, size_t len);
int setdomainname_impl(const char *name, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_UTS_H */
