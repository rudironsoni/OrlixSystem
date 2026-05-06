#include <linux/utsname.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "UTSContract.h"
#include "kernel/cred_internal.h"
#include "kernel/task.h"
#include "kernel/uts.h"

extern int setuid_impl(uid_t uid);
extern void cred_reset_to_defaults(void);

static int expect_errno(int expected) {
    if (errno != expected) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_string(const char *actual, const char *expected) {
    if (strcmp(actual, expected) != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

void uts_contract_reset_state(void) {
    cred_reset_to_defaults();
    uts_reset_current_namespace();
}

int uts_contract_uname_reports_linux_shape(void) {
    struct new_utsname uts;

    uts_contract_reset_state();

    memset(&uts, 0, sizeof(uts));
    if (uname_impl(&uts) != 0) {
        return -1;
    }
    if (expect_string(uts.sysname, "Linux") != 0) {
        return -1;
    }
    if (expect_string(uts.machine, "aarch64") != 0) {
        return -1;
    }
    if (uts.release[0] == '\0' || uts.version[0] == '\0' || uts.nodename[0] == '\0') {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int uts_contract_sethostname_updates_uname_and_gethostname(void) {
    static const char name[] = "ixland-shell-node";
    struct new_utsname uts;
    char observed[__NEW_UTS_LEN + 1];

    uts_contract_reset_state();

    if (sethostname_impl(name, strlen(name)) != 0) {
        return -1;
    }
    memset(&uts, 0, sizeof(uts));
    if (uname_impl(&uts) != 0) {
        return -1;
    }
    if (expect_string(uts.nodename, name) != 0) {
        return -1;
    }
    memset(observed, 0, sizeof(observed));
    if (gethostname_impl(observed, sizeof(observed)) != 0) {
        return -1;
    }
    return expect_string(observed, name);
}

int uts_contract_setdomainname_updates_uname_and_getdomainname(void) {
    static const char name[] = "ixland.local";
    struct new_utsname uts;
    char observed[__NEW_UTS_LEN + 1];

    uts_contract_reset_state();

    if (setdomainname_impl(name, strlen(name)) != 0) {
        return -1;
    }
    memset(&uts, 0, sizeof(uts));
    if (uname_impl(&uts) != 0) {
        return -1;
    }
    if (expect_string(uts.domainname, name) != 0) {
        return -1;
    }
    memset(observed, 0, sizeof(observed));
    if (getdomainname_impl(observed, sizeof(observed)) != 0) {
        return -1;
    }
    return expect_string(observed, name);
}

int uts_contract_sethostname_rejects_oversized_name(void) {
    char name[__NEW_UTS_LEN + 2];

    uts_contract_reset_state();

    memset(name, 'n', sizeof(name));
    errno = 0;
    if (sethostname_impl(name, sizeof(name)) != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(EINVAL);
}

int uts_contract_nonroot_cannot_sethostname(void) {
    static const char name[] = "denied";

    uts_contract_reset_state();

    if (setuid_impl(1000) != 0) {
        return -1;
    }
    errno = 0;
    if (sethostname_impl(name, strlen(name)) != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(EPERM);
}

int uts_contract_child_inherits_parent_uts_namespace(void) {
    static const char name[] = "parent-node";
    struct task_struct *parent;
    struct task_struct *child;
    struct task_struct *saved;
    struct new_utsname uts;
    int result = -1;

    uts_contract_reset_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (sethostname_impl(name, strlen(name)) != 0) {
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = get_current();
    set_current(child);
    memset(&uts, 0, sizeof(uts));
    if (uname_impl(&uts) == 0 && strcmp(uts.nodename, name) == 0) {
        result = 0;
    } else {
        errno = EPROTO;
    }
    set_current(saved);

    task_unlink_child_impl(parent, child);
    free_task(child);
    return result;
}

int uts_contract_unshare_isolates_child_uts_namespace(void) {
    static const char parent_name[] = "parent-node";
    static const char child_name[] = "child-node";
    struct task_struct *parent;
    struct task_struct *child;
    struct task_struct *saved;
    struct new_utsname uts;
    int result = -1;

    uts_contract_reset_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (sethostname_impl(parent_name, strlen(parent_name)) != 0) {
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = get_current();
    set_current(child);
    if (uts_unshare_current() != 0) {
        goto out;
    }
    if (sethostname_impl(child_name, strlen(child_name)) != 0) {
        goto out;
    }
    memset(&uts, 0, sizeof(uts));
    if (uname_impl(&uts) != 0 || strcmp(uts.nodename, child_name) != 0) {
        errno = EPROTO;
        goto out;
    }

    set_current(parent);
    memset(&uts, 0, sizeof(uts));
    if (uname_impl(&uts) != 0 || strcmp(uts.nodename, parent_name) != 0) {
        errno = EPROTO;
        goto out_restore_child;
    }

    result = 0;

out_restore_child:
    set_current(child);
out:
    set_current(saved);
    task_unlink_child_impl(parent, child);
    free_task(child);
    return result;
}
