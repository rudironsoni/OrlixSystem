/* OrlixKernel/kernel/uts.h
 * Private internal header for virtual UTS namespace state.
 *
 * This is runtime-owned namespace state, not a Linux UAPI mirror.
 */

#ifndef KERNEL_UTS_H
#define KERNEL_UTS_H

#include <linux/types.h>
#include <linux/stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct new_utsname;
struct uts_state;

struct uts_state *uts_get_initial_namespace(void);
struct uts_state *uts_get(struct uts_state *ns);
void uts_put(struct uts_state *ns);
struct uts_state *uts_dup(struct uts_state *ns);
uint64_t uts_namespace_id(struct uts_state *ns);
uint64_t uts_namespace_owner_user_ns_id(struct uts_state *ns);
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
