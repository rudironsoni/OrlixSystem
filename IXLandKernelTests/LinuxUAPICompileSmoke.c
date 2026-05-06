/*
 * LinuxUAPICompileSmoke.c
 * IXLandSystemTests
 *
 * LINUX UAPI / ABI COMPILE TEST
 *
 * This file proves that vendored Linux UAPI headers resolve correctly
 * through canonical include paths. It is a pure C compile-smoke test.
 *
 * Purpose:
 *   - prove vendored Linux UAPI headers resolve through canonical include paths
 *   - prove the vendored Linux UAPI headers are actually usable
 *
 * Allowed includes:
 *   - <linux/...>
 *   - <asm/...>
 *   - <asm-generic/...>
 *
 * Forbidden:
 *   - XCTest / Foundation / Objective-C
 *   - pthread.h
 *   - kernel/task.h or kernel/signal.h (private headers)
 *   - Darwin signal/wait/unistd headers
 *   - path traversal into third_party/linux
 */

#include <linux/wait.h>
#include <asm/signal.h>
#include <asm-generic/errno-base.h>

/* Static assertions proving vendored UAPI constants exist and are usable.
 * These are compile-time proofs only, not runtime tests. */

/* linux/wait.h provides WNOHANG, WUNTRACED, WCONTINUED */
#ifndef WNOHANG
#error "WNOHANG not defined from linux/wait.h"
#endif
#ifndef WUNTRACED
#error "WUNTRACED not defined from linux/wait.h"
#endif
#ifndef WCONTINUED
#error "WCONTINUED not defined from linux/wait.h"
#endif

/* asm-generic/signal-defs.h (via asm/signal.h) provides SIG_BLOCK etc. */
#ifndef SIG_BLOCK
#error "SIG_BLOCK not defined from asm/signal.h"
#endif
#ifndef SIG_UNBLOCK
#error "SIG_UNBLOCK not defined from asm/signal.h"
#endif
#ifndef SIG_SETMASK
#error "SIG_SETMASK not defined from asm/signal.h"
#endif

/* asm-generic/errno-base.h provides errno constants */
#ifndef EPERM
#error "EPERM not defined from asm-generic/errno-base.h"
#endif
#ifndef ESRCH
#error "ESRCH not defined from asm-generic/errno-base.h"
#endif
#ifndef EINVAL
#error "EINVAL not defined from asm-generic/errno-base.h"
#endif
#ifndef ECHILD
#error "ECHILD not defined from asm-generic/errno-base.h"
#endif

/* _NSIG from asm/signal.h */
#ifndef _NSIG
#error "_NSIG not defined from asm/signal.h"
#endif

/* Dummy function referencing UAPI types and constants to prove linkage */
static int uapi_compile_smoke_wait_flags(int flags) {
    return (flags & (WNOHANG | WUNTRACED | WCONTINUED));
}

static int uapi_compile_smoke_sigprocmask_op(int how) {
    return (how == SIG_BLOCK || how == SIG_UNBLOCK || how == SIG_SETMASK);
}

static int uapi_compile_smoke_errno_check(int err) {
    return (err == EPERM || err == ESRCH || err == EINVAL || err == ECHILD);
}

static int uapi_compile_smoke_nsig_check(void) {
    return _NSIG;
}

/* Suppress unused-function warnings; these static functions exist purely
 * to prove that UAPI types and constants compile.  The volatile pointer trick
 * ensures the compiler must keep them and cannot discard them as dead code. */
__attribute__((unused)) static void (*volatile uapi_smoke_refs[])(void) = {
    (void (*)(void))uapi_compile_smoke_wait_flags,
    (void (*)(void))uapi_compile_smoke_sigprocmask_op,
    (void (*)(void))uapi_compile_smoke_errno_check,
    (void (*)(void))uapi_compile_smoke_nsig_check,
};
