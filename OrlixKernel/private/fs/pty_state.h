#ifndef PRIVATE_FS_PTY_STATE_H
#define PRIVATE_FS_PTY_STATE_H

#include <linux/stddef.h>
#include <asm/termbits.h>
#include <asm/termios.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task;

int pty_allocate_pair_impl(unsigned int *pty_index);
int pty_open_slave_by_path_impl(const char *path, unsigned int *pty_index);
int pty_open_controlling_slave_impl(unsigned int *pty_index);
bool pty_is_virtual_slave_path_impl(const char *path);
int pty_lookup_slave_path_impl(const char *path, unsigned int *pty_index);
size_t pty_list_slave_indices_impl(unsigned int *indices, size_t capacity);

int pty_close_end_impl(unsigned int pty_index, bool is_master);

__kernel_ssize_t pty_read_master_impl(unsigned int pty_index, void *buf, size_t count,
                                      bool nonblock);
__kernel_ssize_t pty_write_master_impl(unsigned int pty_index, const void *buf, size_t count,
                                       bool nonblock);
__kernel_ssize_t pty_read_slave_impl(unsigned int pty_index, void *buf, size_t count,
                                     bool nonblock);
__kernel_ssize_t pty_write_slave_impl(unsigned int pty_index, const void *buf, size_t count,
                                      bool nonblock);

short pty_poll_revents_impl(unsigned int pty_index, bool is_master, short events);
void pty_poll_wake_impl(unsigned int pty_index);
int pty_get_readable_bytes_impl(unsigned int pty_index, bool is_master, int *bytes);

int pty_set_lock_impl(unsigned int pty_index, bool locked);
int pty_get_lock_impl(unsigned int pty_index, int *locked);

int pty_get_termios_impl(unsigned int pty_index, struct termios *termios);
int pty_set_termios_impl(unsigned int pty_index, const struct termios *termios);
int pty_set_termios_with_action_impl(unsigned int pty_index, const struct termios *termios,
                                     int action);

int pty_get_winsize_impl(unsigned int pty_index, struct winsize *winsize);
int pty_set_winsize_impl(unsigned int pty_index, const struct winsize *winsize);

int pty_get_foreground_pgrp_impl(unsigned int pty_index, int32_t *pgrp);
int pty_get_controlling_sid_impl(unsigned int pty_index, int32_t *sid);
int pty_set_foreground_pgrp_impl(unsigned int pty_index, int32_t pgrp);
int pty_set_controlling_tty_impl(unsigned int pty_index, int arg);
int pty_detach_controlling_tty_impl(void);
void pty_session_leader_exit_impl(struct task *task);

int pty_format_slave_path_impl(unsigned int pty_index, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif
