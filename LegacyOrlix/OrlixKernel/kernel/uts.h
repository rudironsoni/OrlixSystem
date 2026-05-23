/* OrlixKernel/kernel/uts.h
 * Private internal header for virtual UTS namespace state.
 *
 * This is runtime-owned namespace state, not a Upstream Linux ABI mirror.
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

int uname_impl(struct new_utsname *buf);
int gethostname_impl(char *name, size_t len);
int sethostname_impl(const char *name, size_t len);
int getdomainname_impl(char *name, size_t len);
int setdomainname_impl(const char *name, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_UTS_H */
