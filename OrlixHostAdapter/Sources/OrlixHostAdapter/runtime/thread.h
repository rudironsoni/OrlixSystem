#ifndef ORLIX_HOST_ADAPTER_RUNTIME_THREAD_H
#define ORLIX_HOST_ADAPTER_RUNTIME_THREAD_H

__attribute__((visibility("hidden"))) void orlix_host_thread_idle(void);
__attribute__((visibility("hidden"))) void orlix_host_thread_idle_until(unsigned long long deadline_ns);

#endif /* ORLIX_HOST_ADAPTER_RUNTIME_THREAD_H */
