#include <linux/futex.h>

#ifndef FUTEX_WAIT
#error "FUTEX_WAIT must be defined by linux/futex.h"
#endif

#ifndef FUTEX_WAKE
#error "FUTEX_WAKE must be defined by linux/futex.h"
#endif

#ifndef FUTEX_WAIT_BITSET
#error "FUTEX_WAIT_BITSET must be defined by linux/futex.h"
#endif

#ifndef FUTEX_WAKE_BITSET
#error "FUTEX_WAKE_BITSET must be defined by linux/futex.h"
#endif

static int futex_uapi_smoke_wait(int op) {
    return (op == FUTEX_WAIT || op == FUTEX_WAIT_BITSET);
}

static int futex_uapi_smoke_wake(int op) {
    return (op == FUTEX_WAKE || op == FUTEX_WAKE_BITSET);
}

__attribute__((unused)) static void (*volatile futex_uapi_refs[])(void) = {
    (void (*)(void))futex_uapi_smoke_wait,
    (void (*)(void))futex_uapi_smoke_wake,
};
