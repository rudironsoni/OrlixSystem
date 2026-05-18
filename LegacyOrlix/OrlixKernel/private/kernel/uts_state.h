#ifndef PRIVATE_KERNEL_UTS_STATE_H
#define PRIVATE_KERNEL_UTS_STATE_H

#include "kernel/uts.h"

#ifdef __cplusplus
extern "C" {
#endif

struct uts_state *uts_get_initial_namespace(void);
struct uts_state *uts_get(struct uts_state *ns);
void uts_put(struct uts_state *ns);
struct uts_state *uts_dup(struct uts_state *ns);
uint64_t uts_namespace_id(struct uts_state *ns);
uint64_t uts_namespace_owner_user_ns_id(struct uts_state *ns);
int uts_unshare_current(void);
void uts_reset_initial_namespace(void);
void uts_reset_current_namespace(void);

#ifdef __cplusplus
}
#endif

#endif
