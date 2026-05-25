#ifndef ORLIX_HOST_ADAPTER_RUNTIME_HOST_TLS_H
#define ORLIX_HOST_ADAPTER_RUNTIME_HOST_TLS_H

__attribute__((visibility("hidden"))) unsigned long OrlixHostEnterHostTls(void);
__attribute__((visibility("hidden"))) void OrlixHostLeaveHostTls(unsigned long active_tls);

#endif /* ORLIX_HOST_ADAPTER_RUNTIME_HOST_TLS_H */
