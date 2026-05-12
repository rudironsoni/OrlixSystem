#include "uts.h"

#include "cred.h"
#include "task.h"
#include "../private/kernel/task_state.h"

#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/gfp_types.h>
#include <linux/string.h>

#include <uapi/linux/capability.h>
#include <uapi/linux/utsname.h>

extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);

struct uts_state {
    atomic_t refs;
    uint64_t ns_id;
    uint64_t owner_user_ns_id;
    bool static_storage;
    kernel_mutex_t lock;
    char sysname[__NEW_UTS_LEN + 1];
    char nodename[__NEW_UTS_LEN + 1];
    char release[__NEW_UTS_LEN + 1];
    char version[__NEW_UTS_LEN + 1];
    char machine[__NEW_UTS_LEN + 1];
    char domainname[__NEW_UTS_LEN + 1];
};

static struct uts_state initial_uts_ns;
static atomic_t initial_uts_ns_ready = ATOMIC_INIT(0);
static kernel_mutex_t initial_uts_ns_lock = KERNEL_MUTEX_INITIALIZER;
static atomic_t next_uts_ns_id = ATOMIC_INIT(1);

static void uts_copy_literal(char dst[__NEW_UTS_LEN + 1], const char *src) {
    size_t len = strlen(src);
    if (len > __NEW_UTS_LEN) {
        len = __NEW_UTS_LEN;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void uts_set_defaults_locked(struct uts_state *ns) {
    uts_copy_literal(ns->sysname, "Linux");
    uts_copy_literal(ns->nodename, "orlix");
    uts_copy_literal(ns->release, "6.12.0-orlix");
    uts_copy_literal(ns->version, "#1 OrlixKernel");
    uts_copy_literal(ns->machine, "aarch64");
    uts_copy_literal(ns->domainname, "(none)");
}

static void uts_init_initial_namespace(void) {
    kernel_mutex_lock(&initial_uts_ns_lock);
    if (atomic_read(&initial_uts_ns_ready) == 0) {
        atomic_set(&initial_uts_ns.refs, 1);
        initial_uts_ns.ns_id = (uint64_t)(atomic_inc_return(&next_uts_ns_id) - 1);
        initial_uts_ns.owner_user_ns_id = 1;
        initial_uts_ns.static_storage = true;
        kernel_mutex_init(&initial_uts_ns.lock);
        kernel_mutex_lock(&initial_uts_ns.lock);
        uts_set_defaults_locked(&initial_uts_ns);
        kernel_mutex_unlock(&initial_uts_ns.lock);
        atomic_set(&initial_uts_ns_ready, 1);
    }
    kernel_mutex_unlock(&initial_uts_ns_lock);
}

struct uts_state *uts_get_initial_namespace(void) {
    uts_init_initial_namespace();
    return uts_get(&initial_uts_ns);
}

struct uts_state *uts_get(struct uts_state *ns) {
    if (!ns) {
        return NULL;
    }
    atomic_inc(&ns->refs);
    return ns;
}

void uts_put(struct uts_state *ns) {
    if (!ns) {
        return;
    }
    if (atomic_dec_return(&ns->refs) >= 1) {
        return;
    }
    if (ns->static_storage) {
        atomic_set(&ns->refs, 1);
        return;
    }
    kernel_mutex_destroy(&ns->lock);
    kfree(ns);
}

struct uts_state *uts_dup(struct uts_state *ns) {
    struct uts_state *copy;

    if (!ns) {
        ns = uts_get_initial_namespace();
        if (!ns) {
            return NULL;
        }
        uts_put(ns);
    }

    copy = __kmalloc_noprof(sizeof(*copy), GFP_KERNEL | __GFP_ZERO);
    if (!copy) {
        return NULL;
    }

    atomic_set(&copy->refs, 1);
    copy->ns_id = (uint64_t)(atomic_inc_return(&next_uts_ns_id) - 1);
    copy->owner_user_ns_id = cred_user_namespace_id(cred_current());
    if (copy->owner_user_ns_id == 0) {
        copy->owner_user_ns_id = ns->owner_user_ns_id ? ns->owner_user_ns_id : 1;
    }
    copy->static_storage = false;
    kernel_mutex_init(&copy->lock);

    kernel_mutex_lock(&ns->lock);
    memcpy(copy->sysname, ns->sysname, sizeof(copy->sysname));
    memcpy(copy->nodename, ns->nodename, sizeof(copy->nodename));
    memcpy(copy->release, ns->release, sizeof(copy->release));
    memcpy(copy->version, ns->version, sizeof(copy->version));
    memcpy(copy->machine, ns->machine, sizeof(copy->machine));
    memcpy(copy->domainname, ns->domainname, sizeof(copy->domainname));
    kernel_mutex_unlock(&ns->lock);

    return copy;
}

uint64_t uts_namespace_id(struct uts_state *ns) {
    if (!ns) {
        uts_init_initial_namespace();
        ns = &initial_uts_ns;
    }
    return ns->ns_id;
}

uint64_t uts_namespace_owner_user_ns_id(struct uts_state *ns) {
    if (!ns) {
        uts_init_initial_namespace();
        ns = &initial_uts_ns;
    }
    return ns->owner_user_ns_id ? ns->owner_user_ns_id : 1;
}

void uts_reset_initial_namespace(void) {
    uts_init_initial_namespace();
    kernel_mutex_lock(&initial_uts_ns.lock);
    uts_set_defaults_locked(&initial_uts_ns);
    kernel_mutex_unlock(&initial_uts_ns.lock);
}

void uts_reset_current_namespace(void) {
    struct task *task;

    uts_reset_initial_namespace();

    task = task_current();
    if (!task) {
        return;
    }

    if (task->uts_ns) {
        uts_put(task->uts_ns);
    }
    task->uts_ns = uts_get_initial_namespace();
}

static struct uts_state *current_uts_namespace(void) {
    struct task *task = task_current();
    if (task && task->uts_ns) {
        return task->uts_ns;
    }
    return &initial_uts_ns;
}

static int uts_copy_from_namespace(char *dst, size_t len, const char *src) {
    size_t srclen;

    if (!dst && len > 0) {
        return -EFAULT;
    }
    if (len == 0) {
        return 0;
    }

    srclen = strlen(src);
    if (len <= srclen) {
        memcpy(dst, src, len);
        return -ENAMETOOLONG;
    }

    memcpy(dst, src, srclen + 1);
    return 0;
}

static int uts_set_name(struct uts_state *ns, char dst[__NEW_UTS_LEN + 1],
                        const char *src, size_t len) {
    struct cred *cred;

    if (!src && len > 0) {
        return -EFAULT;
    }
    if (len > __NEW_UTS_LEN) {
        return -EINVAL;
    }

    cred = cred_current();
    if (!cred_has_cap_in_user_namespace(cred, uts_namespace_owner_user_ns_id(ns), CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return 0;
}

int uname_impl(struct new_utsname *buf) {
    struct uts_state *ns;

    if (!buf) {
        return -EFAULT;
    }

    uts_init_initial_namespace();
    ns = current_uts_namespace();

    kernel_mutex_lock(&ns->lock);
    memcpy(buf->sysname, ns->sysname, sizeof(buf->sysname));
    memcpy(buf->nodename, ns->nodename, sizeof(buf->nodename));
    memcpy(buf->release, ns->release, sizeof(buf->release));
    memcpy(buf->version, ns->version, sizeof(buf->version));
    memcpy(buf->machine, ns->machine, sizeof(buf->machine));
    memcpy(buf->domainname, ns->domainname, sizeof(buf->domainname));
    kernel_mutex_unlock(&ns->lock);
    return 0;
}

int gethostname_impl(char *name, size_t len) {
    struct uts_state *ns;
    int result;

    uts_init_initial_namespace();
    ns = current_uts_namespace();

    kernel_mutex_lock(&ns->lock);
    result = uts_copy_from_namespace(name, len, ns->nodename);
    kernel_mutex_unlock(&ns->lock);
    return result;
}

int sethostname_impl(const char *name, size_t len) {
    struct uts_state *ns;
    int result;

    uts_init_initial_namespace();
    ns = current_uts_namespace();

    kernel_mutex_lock(&ns->lock);
    result = uts_set_name(ns, ns->nodename, name, len);
    kernel_mutex_unlock(&ns->lock);
    return result;
}

int getdomainname_impl(char *name, size_t len) {
    struct uts_state *ns;
    int result;

    uts_init_initial_namespace();
    ns = current_uts_namespace();

    kernel_mutex_lock(&ns->lock);
    result = uts_copy_from_namespace(name, len, ns->domainname);
    kernel_mutex_unlock(&ns->lock);
    return result;
}

int setdomainname_impl(const char *name, size_t len) {
    struct uts_state *ns;
    int result;

    uts_init_initial_namespace();
    ns = current_uts_namespace();

    kernel_mutex_lock(&ns->lock);
    result = uts_set_name(ns, ns->domainname, name, len);
    kernel_mutex_unlock(&ns->lock);
    return result;
}

int uts_unshare_current(void) {
    struct task *task = task_current();
    struct uts_state *copy;

    if (!task) {
        return -ESRCH;
    }

    uts_init_initial_namespace();
    copy = uts_dup(current_uts_namespace());
    if (!copy) {
        return -ENOMEM;
    }

    if (task->uts_ns) {
        uts_put(task->uts_ns);
    }
    task->uts_ns = copy;
    return 0;
}
