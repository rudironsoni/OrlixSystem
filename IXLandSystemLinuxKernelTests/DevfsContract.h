#ifndef IXLANDSYSTEMLINUXKERNELTESTS_DEVFSCONTRACT_H
#define IXLANDSYSTEMLINUXKERNELTESTS_DEVFSCONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

int devfs_contract_random_device_is_character_and_readable(void);
int devfs_contract_tty_node_exists_without_controlling_tty(void);
int devfs_contract_tty_open_without_controlling_tty_returns_enxio(void);
int devfs_contract_pts_directory_exists(void);
int devfs_contract_dev_directory_lists_linux_device_nodes(void);
int devfs_contract_allocated_pty_slave_is_visible_in_devpts(void);

#ifdef __cplusplus
}
#endif

#endif /* IXLANDSYSTEMLINUXKERNELTESTS_DEVFSCONTRACT_H */
