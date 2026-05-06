#ifndef IXLANDSYSTEMLINUXKERNELTESTS_UTSCONTRACT_H
#define IXLANDSYSTEMLINUXKERNELTESTS_UTSCONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

void uts_contract_reset_state(void);
int uts_contract_uname_reports_linux_shape(void);
int uts_contract_sethostname_updates_uname_and_gethostname(void);
int uts_contract_setdomainname_updates_uname_and_getdomainname(void);
int uts_contract_sethostname_rejects_oversized_name(void);
int uts_contract_nonroot_cannot_sethostname(void);
int uts_contract_child_inherits_parent_uts_namespace(void);
int uts_contract_unshare_isolates_child_uts_namespace(void);

#ifdef __cplusplus
}
#endif

#endif /* IXLANDSYSTEMLINUXKERNELTESTS_UTSCONTRACT_H */
