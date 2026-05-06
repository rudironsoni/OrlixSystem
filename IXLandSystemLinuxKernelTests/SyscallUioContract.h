#ifndef SYSCALL_UIO_CONTRACT_H
#define SYSCALL_UIO_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

int syscall_uio_contract_readv_writev_round_trip(void);
int syscall_uio_contract_rejects_invalid_iov_count(void);
int syscall_uio_contract_truncate_changes_file_size_by_path(void);
int syscall_uio_contract_preadv_pwritev_preserve_file_offset(void);
int syscall_uio_contract_preadv2_pwritev2_flag_policy(void);
int syscall_uio_contract_sendfile_honors_offset_rules(void);

#ifdef __cplusplus
}
#endif

#endif
