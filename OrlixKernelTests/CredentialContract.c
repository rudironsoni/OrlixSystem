#include <uapi/linux/errno.h>
#include <uapi/linux/securebits.h>

#include "CredentialContract.h"
#include "private/kernel/cred_state.h"
#include "kernel/cred.h"

extern int errno;

int credential_contract_alloc_cred_with_defaults(void) {
    struct cred *cred = alloc_cred();

    if (!cred) {
        errno = ENOMEM;
        return -1;
    }
    if (cred->uid != 0 || cred->gid != 0 ||
        cred->euid != 0 || cred->egid != 0 ||
        cred->suid != 0 || cred->sgid != 0 ||
        cred->fsuid != 0 || cred->fsgid != 0 ||
        cred->group_count != 0 || cred->groups != NULL ||
        cred->no_new_privs || cred->securebits != SECUREBITS_DEFAULT ||
        cred->cap_permitted == 0 || cred->cap_effective == 0 ||
        cred->cap_bounding == 0) {
        free_cred(cred);
        errno = EPROTO;
        return -1;
    }

    free_cred(cred);
    return 0;
}

int credential_contract_cred_reference_counting(void) {
    struct cred *cred = alloc_cred();

    if (!cred) {
        errno = ENOMEM;
        return -1;
    }
    if (cred->refs != 1) {
        free_cred(cred);
        errno = EPROTO;
        return -1;
    }

    get_cred(cred);
    if (cred->refs != 2) {
        put_cred(cred);
        put_cred(cred);
        errno = EPROTO;
        return -1;
    }

    put_cred(cred);
    if (cred->refs != 1) {
        put_cred(cred);
        errno = EPROTO;
        return -1;
    }

    put_cred(cred);
    return 0;
}
