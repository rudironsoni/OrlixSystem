#ifndef INTERNAL_TIMEKEEPING_H
#define INTERNAL_TIMEKEEPING_H

#ifdef __cplusplus
extern "C" {
#endif

int kernel_sleep_ms(int timeout_ms);
int kernel_clock_now_ns(int clock_id, unsigned long long *ns_out);

#ifdef __cplusplus
}
#endif

#endif
