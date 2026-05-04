# Linux 6.12 arm64 Syscall Gap Matrix

Generated from vendored Linux UAPI and `runtime/syscall.c`.

| nr | syscall | classification | priority |
| ---: | --- | --- | --- |
| 0 | `io_setup` | kernel-owned missing:unclassified | package |
| 1 | `io_destroy` | kernel-owned missing:unclassified | package |
| 2 | `io_submit` | kernel-owned missing:unclassified | package |
| 3 | `io_cancel` | kernel-owned missing:unclassified | package |
| 4 | `io_getevents` | kernel-owned missing:unclassified | package |
| 5 | `setxattr` | implemented:xattr | none |
| 6 | `lsetxattr` | implemented:xattr | none |
| 7 | `fsetxattr` | implemented:xattr | none |
| 8 | `getxattr` | implemented:xattr | none |
| 9 | `lgetxattr` | implemented:xattr | none |
| 10 | `fgetxattr` | implemented:xattr | none |
| 11 | `listxattr` | implemented:xattr | none |
| 12 | `llistxattr` | implemented:xattr | none |
| 13 | `flistxattr` | implemented:xattr | none |
| 14 | `removexattr` | implemented:xattr | none |
| 15 | `lremovexattr` | implemented:xattr | none |
| 16 | `fremovexattr` | implemented:xattr | none |
| 17 | `getcwd` | implemented:fd | none |
| 18 | `lookup_dcookie` | kernel-owned missing:unclassified | package |
| 19 | `eventfd2` | implemented:readiness | none |
| 20 | `epoll_create1` | implemented:readiness | none |
| 21 | `epoll_ctl` | implemented:readiness | none |
| 22 | `epoll_pwait` | implemented:readiness | none |
| 23 | `dup` | implemented:fd | none |
| 24 | `dup3` | implemented:fd | none |
| 25 | `fcntl` | implemented:fd | none |
| 26 | `inotify_init1` | kernel-owned missing:unclassified | package |
| 27 | `inotify_add_watch` | kernel-owned missing:unclassified | package |
| 28 | `inotify_rm_watch` | kernel-owned missing:unclassified | package |
| 29 | `ioctl` | implemented:fd | none |
| 30 | `ioprio_set` | kernel-owned missing:unclassified | package |
| 31 | `ioprio_get` | kernel-owned missing:unclassified | package |
| 32 | `flock` | implemented:fd | none |
| 33 | `mknodat` | kernel-owned missing:unclassified | package |
| 34 | `mkdirat` | implemented:fd | none |
| 35 | `unlinkat` | implemented:fd | none |
| 36 | `symlinkat` | kernel-owned missing:unclassified | package |
| 37 | `linkat` | kernel-owned missing:unclassified | package |
| 38 | `renameat` | implemented:fd | none |
| 39 | `umount2` | kernel-owned missing:unclassified | package |
| 40 | `mount` | kernel-owned missing:unclassified | package |
| 41 | `pivot_root` | implemented:mount | none |
| 42 | `nfsservctl` | kernel-owned missing:unclassified | package |
| 43 | `statfs` | implemented:fd | none |
| 44 | `fstatfs` | implemented:fd | none |
| 45 | `truncate` | kernel-owned missing:unclassified | package |
| 46 | `ftruncate` | implemented:fd | none |
| 47 | `fallocate` | kernel-owned missing:unclassified | package |
| 48 | `faccessat` | implemented:fd | none |
| 49 | `chdir` | implemented:fd | none |
| 50 | `fchdir` | implemented:fd | none |
| 51 | `chroot` | kernel-owned missing:unclassified | package |
| 52 | `fchmod` | implemented:fd | none |
| 53 | `fchmodat` | implemented:fd | none |
| 54 | `fchownat` | implemented:fd | none |
| 55 | `fchown` | implemented:fd | none |
| 56 | `openat` | implemented:fd | none |
| 57 | `close` | implemented:fd | none |
| 58 | `vhangup` | kernel-owned missing:unclassified | package |
| 59 | `pipe2` | implemented:fd | none |
| 60 | `quotactl` | kernel-owned missing:unclassified | package |
| 61 | `getdents64` | implemented:fd | none |
| 62 | `lseek` | implemented:fd | none |
| 63 | `read` | implemented:fd | none |
| 64 | `write` | implemented:fd | none |
| 65 | `readv` | implemented:fd | none |
| 66 | `writev` | implemented:fd | none |
| 67 | `pread64` | implemented:fd | none |
| 68 | `pwrite64` | implemented:fd | none |
| 69 | `preadv` | kernel-owned missing:unclassified | package |
| 70 | `pwritev` | kernel-owned missing:unclassified | package |
| 71 | `sendfile` | kernel-owned missing:unclassified | package |
| 72 | `pselect6` | implemented:readiness | none |
| 73 | `ppoll` | implemented:readiness | none |
| 74 | `signalfd4` | kernel-owned missing:unclassified | package |
| 75 | `vmsplice` | kernel-owned missing:unclassified | package |
| 76 | `splice` | kernel-owned missing:unclassified | package |
| 77 | `tee` | kernel-owned missing:unclassified | package |
| 78 | `readlinkat` | implemented:fd | none |
| 79 | `newfstatat` | implemented:fd | none |
| 80 | `fstat` | implemented:fd | none |
| 81 | `sync` | implemented:fd | none |
| 82 | `fsync` | implemented:fd | none |
| 83 | `fdatasync` | implemented:fd | none |
| 84 | `sync_file_range` | kernel-owned missing:unclassified | package |
| 85 | `timerfd_create` | implemented:readiness | none |
| 86 | `timerfd_settime` | implemented:readiness | none |
| 87 | `timerfd_gettime` | implemented:readiness | none |
| 88 | `utimensat` | implemented:fd | none |
| 89 | `acct` | kernel-owned missing:unclassified | package |
| 90 | `capget` | kernel-owned missing:unclassified | package |
| 91 | `capset` | kernel-owned missing:unclassified | package |
| 92 | `personality` | kernel-owned missing:unclassified | package |
| 93 | `exit` | implemented:process | none |
| 94 | `exit_group` | implemented:process | none |
| 95 | `waitid` | implemented:process | none |
| 96 | `set_tid_address` | implemented:process | none |
| 97 | `unshare` | kernel-owned missing:unclassified | package |
| 98 | `futex` | implemented:readiness | none |
| 99 | `set_robust_list` | implemented:resource | none |
| 100 | `get_robust_list` | implemented:resource | none |
| 101 | `nanosleep` | implemented:time | none |
| 102 | `getitimer` | implemented:time | none |
| 103 | `setitimer` | implemented:time | none |
| 104 | `kexec_load` | kernel-owned missing:unclassified | package |
| 105 | `init_module` | kernel-owned missing:unclassified | package |
| 106 | `delete_module` | kernel-owned missing:unclassified | package |
| 107 | `timer_create` | kernel-owned missing:unclassified | package |
| 108 | `timer_gettime` | kernel-owned missing:unclassified | package |
| 109 | `timer_getoverrun` | kernel-owned missing:unclassified | package |
| 110 | `timer_settime` | kernel-owned missing:unclassified | package |
| 111 | `timer_delete` | kernel-owned missing:unclassified | package |
| 112 | `clock_settime` | kernel-owned missing:unclassified | package |
| 113 | `clock_gettime` | implemented:time | none |
| 114 | `clock_getres` | kernel-owned missing:unclassified | package |
| 115 | `clock_nanosleep` | implemented:time | none |
| 116 | `syslog` | kernel-owned missing:unclassified | package |
| 117 | `ptrace` | kernel-owned missing:unclassified | package |
| 118 | `sched_setparam` | kernel-owned missing:unclassified | package |
| 119 | `sched_setscheduler` | kernel-owned missing:unclassified | package |
| 120 | `sched_getscheduler` | kernel-owned missing:unclassified | package |
| 121 | `sched_getparam` | kernel-owned missing:unclassified | package |
| 122 | `sched_setaffinity` | kernel-owned missing:unclassified | package |
| 123 | `sched_getaffinity` | kernel-owned missing:unclassified | package |
| 124 | `sched_yield` | kernel-owned missing:unclassified | package |
| 125 | `sched_get_priority_max` | kernel-owned missing:unclassified | package |
| 126 | `sched_get_priority_min` | kernel-owned missing:unclassified | package |
| 127 | `sched_rr_get_interval` | kernel-owned missing:unclassified | package |
| 128 | `restart_syscall` | implemented:signal | none |
| 129 | `kill` | implemented:signal | none |
| 130 | `tkill` | kernel-owned missing:unclassified | package |
| 131 | `tgkill` | implemented:signal | none |
| 132 | `sigaltstack` | implemented:signal | none |
| 133 | `rt_sigsuspend` | kernel-owned missing:unclassified | package |
| 134 | `rt_sigaction` | implemented:signal | none |
| 135 | `rt_sigprocmask` | implemented:signal | none |
| 136 | `rt_sigpending` | kernel-owned missing:unclassified | package |
| 137 | `rt_sigtimedwait` | kernel-owned missing:unclassified | package |
| 138 | `rt_sigqueueinfo` | kernel-owned missing:unclassified | package |
| 139 | `rt_sigreturn` | implemented:signal | none |
| 140 | `setpriority` | kernel-owned missing:unclassified | package |
| 141 | `getpriority` | kernel-owned missing:unclassified | package |
| 142 | `reboot` | kernel-owned missing:unclassified | package |
| 143 | `setregid` | implemented:process | none |
| 144 | `setgid` | implemented:process | none |
| 145 | `setreuid` | implemented:process | none |
| 146 | `setuid` | implemented:process | none |
| 147 | `setresuid` | implemented:process | none |
| 148 | `getresuid` | implemented:process | none |
| 149 | `setresgid` | implemented:process | none |
| 150 | `getresgid` | implemented:process | none |
| 151 | `setfsuid` | kernel-owned missing:unclassified | package |
| 152 | `setfsgid` | kernel-owned missing:unclassified | package |
| 153 | `times` | implemented:resource | none |
| 154 | `setpgid` | implemented:process | none |
| 155 | `getpgid` | implemented:process | none |
| 156 | `getsid` | implemented:process | none |
| 157 | `setsid` | implemented:process | none |
| 158 | `getgroups` | implemented:process | none |
| 159 | `setgroups` | implemented:process | none |
| 160 | `uname` | implemented:process | none |
| 161 | `sethostname` | kernel-owned missing:unclassified | package |
| 162 | `setdomainname` | kernel-owned missing:unclassified | package |
| 163 | `getrlimit` | kernel-owned missing:unclassified | package |
| 164 | `setrlimit` | kernel-owned missing:unclassified | package |
| 165 | `getrusage` | implemented:resource | none |
| 166 | `umask` | implemented:fd | none |
| 167 | `prctl` | implemented:process | none |
| 168 | `getcpu` | kernel-owned missing:unclassified | package |
| 169 | `gettimeofday` | implemented:time | none |
| 170 | `settimeofday` | kernel-owned missing:unclassified | package |
| 171 | `adjtimex` | kernel-owned missing:unclassified | package |
| 172 | `getpid` | implemented:process | none |
| 173 | `getppid` | implemented:process | none |
| 174 | `getuid` | implemented:process | none |
| 175 | `geteuid` | implemented:process | none |
| 176 | `getgid` | implemented:process | none |
| 177 | `getegid` | implemented:process | none |
| 178 | `gettid` | implemented:process | none |
| 179 | `sysinfo` | kernel-owned missing:unclassified | package |
| 180 | `mq_open` | kernel-owned missing:unclassified | package |
| 181 | `mq_unlink` | kernel-owned missing:unclassified | package |
| 182 | `mq_timedsend` | kernel-owned missing:unclassified | package |
| 183 | `mq_timedreceive` | kernel-owned missing:unclassified | package |
| 184 | `mq_notify` | kernel-owned missing:unclassified | package |
| 185 | `mq_getsetattr` | kernel-owned missing:unclassified | package |
| 186 | `msgget` | kernel-owned missing:unclassified | package |
| 187 | `msgctl` | kernel-owned missing:unclassified | package |
| 188 | `msgrcv` | kernel-owned missing:unclassified | package |
| 189 | `msgsnd` | kernel-owned missing:unclassified | package |
| 190 | `semget` | kernel-owned missing:unclassified | package |
| 191 | `semctl` | kernel-owned missing:unclassified | package |
| 192 | `semtimedop` | kernel-owned missing:unclassified | package |
| 193 | `semop` | kernel-owned missing:unclassified | package |
| 194 | `shmget` | kernel-owned missing:unclassified | package |
| 195 | `shmctl` | kernel-owned missing:unclassified | package |
| 196 | `shmat` | kernel-owned missing:unclassified | package |
| 197 | `shmdt` | kernel-owned missing:unclassified | package |
| 198 | `socket` | future backend:virtual-network | network |
| 199 | `socketpair` | future backend:virtual-network | network |
| 200 | `bind` | kernel-owned missing:unclassified | package |
| 201 | `listen` | kernel-owned missing:unclassified | package |
| 202 | `accept` | kernel-owned missing:unclassified | package |
| 203 | `connect` | future backend:virtual-network | network |
| 204 | `getsockname` | kernel-owned missing:unclassified | package |
| 205 | `getpeername` | kernel-owned missing:unclassified | package |
| 206 | `sendto` | future backend:virtual-network | network |
| 207 | `recvfrom` | future backend:virtual-network | network |
| 208 | `setsockopt` | kernel-owned missing:unclassified | package |
| 209 | `getsockopt` | kernel-owned missing:unclassified | package |
| 210 | `shutdown` | kernel-owned missing:unclassified | package |
| 211 | `sendmsg` | future backend:virtual-network | network |
| 212 | `recvmsg` | future backend:virtual-network | network |
| 213 | `readahead` | kernel-owned missing:unclassified | package |
| 214 | `brk` | implemented:vm | none |
| 215 | `munmap` | implemented:vm | none |
| 216 | `mremap` | implemented:vm | none |
| 217 | `add_key` | kernel-owned missing:unclassified | package |
| 218 | `request_key` | kernel-owned missing:unclassified | package |
| 219 | `keyctl` | kernel-owned missing:unclassified | package |
| 220 | `clone` | implemented:process | none |
| 221 | `execve` | implemented:process | none |
| 222 | `mmap` | implemented:vm | none |
| 223 | `fadvise64` | kernel-owned missing:unclassified | package |
| 224 | `swapon` | kernel-owned missing:unclassified | package |
| 225 | `swapoff` | kernel-owned missing:unclassified | package |
| 226 | `mprotect` | implemented:vm | none |
| 227 | `msync` | implemented:vm | none |
| 228 | `mlock` | kernel-owned missing:unclassified | package |
| 229 | `munlock` | kernel-owned missing:unclassified | package |
| 230 | `mlockall` | kernel-owned missing:unclassified | package |
| 231 | `munlockall` | kernel-owned missing:unclassified | package |
| 232 | `mincore` | implemented:vm | none |
| 233 | `madvise` | implemented:vm | none |
| 234 | `remap_file_pages` | kernel-owned missing:unclassified | package |
| 235 | `mbind` | kernel-owned missing:unclassified | package |
| 236 | `get_mempolicy` | kernel-owned missing:unclassified | package |
| 237 | `set_mempolicy` | kernel-owned missing:unclassified | package |
| 238 | `migrate_pages` | kernel-owned missing:unclassified | package |
| 239 | `move_pages` | kernel-owned missing:unclassified | package |
| 240 | `rt_tgsigqueueinfo` | kernel-owned missing:unclassified | package |
| 241 | `perf_event_open` | kernel-owned missing:unclassified | package |
| 242 | `accept4` | kernel-owned missing:unclassified | package |
| 243 | `recvmmsg` | future backend:virtual-network | network |
| 260 | `wait4` | implemented:process | none |
| 261 | `prlimit64` | implemented:resource | none |
| 262 | `fanotify_init` | kernel-owned missing:unclassified | package |
| 263 | `fanotify_mark` | kernel-owned missing:unclassified | package |
| 264 | `name_to_handle_at` | kernel-owned missing:unclassified | package |
| 265 | `open_by_handle_at` | kernel-owned missing:unclassified | package |
| 266 | `clock_adjtime` | kernel-owned missing:unclassified | package |
| 267 | `syncfs` | implemented:fd | none |
| 268 | `setns` | kernel-owned missing:unclassified | package |
| 269 | `sendmmsg` | future backend:virtual-network | network |
| 270 | `process_vm_readv` | kernel-owned missing:unclassified | package |
| 271 | `process_vm_writev` | kernel-owned missing:unclassified | package |
| 272 | `kcmp` | kernel-owned missing:unclassified | package |
| 273 | `finit_module` | kernel-owned missing:unclassified | package |
| 274 | `sched_setattr` | kernel-owned missing:unclassified | package |
| 275 | `sched_getattr` | kernel-owned missing:unclassified | package |
| 276 | `renameat2` | implemented:fd | none |
| 277 | `seccomp` | kernel-owned missing:unclassified | package |
| 278 | `getrandom` | implemented:random | none |
| 279 | `memfd_create` | implemented:fd | none |
| 280 | `bpf` | kernel-owned missing:unclassified | package |
| 281 | `execveat` | implemented:process | none |
| 282 | `userfaultfd` | kernel-owned missing:unclassified | package |
| 283 | `membarrier` | kernel-owned missing:unclassified | package |
| 284 | `mlock2` | kernel-owned missing:unclassified | package |
| 285 | `copy_file_range` | implemented:fd | none |
| 286 | `preadv2` | kernel-owned missing:unclassified | package |
| 287 | `pwritev2` | kernel-owned missing:unclassified | package |
| 288 | `pkey_mprotect` | kernel-owned missing:unclassified | package |
| 289 | `pkey_alloc` | kernel-owned missing:unclassified | package |
| 290 | `pkey_free` | kernel-owned missing:unclassified | package |
| 291 | `statx` | implemented:fd | none |
| 292 | `io_pgetevents` | kernel-owned missing:unclassified | package |
| 293 | `rseq` | kernel-owned missing:unclassified | package |
| 294 | `kexec_file_load` | kernel-owned missing:unclassified | package |
| 424 | `pidfd_send_signal` | kernel-owned missing:unclassified | package |
| 425 | `io_uring_setup` | kernel-owned missing:unclassified | package |
| 426 | `io_uring_enter` | kernel-owned missing:unclassified | package |
| 427 | `io_uring_register` | kernel-owned missing:unclassified | package |
| 428 | `open_tree` | implemented:mount | none |
| 429 | `move_mount` | implemented:mount | none |
| 430 | `fsopen` | kernel-owned missing:unclassified | package |
| 431 | `fsconfig` | kernel-owned missing:unclassified | package |
| 432 | `fsmount` | kernel-owned missing:unclassified | package |
| 433 | `fspick` | kernel-owned missing:unclassified | package |
| 434 | `pidfd_open` | implemented:process | none |
| 435 | `clone3` | implemented:process | none |
| 436 | `close_range` | implemented:fd | none |
| 437 | `openat2` | implemented:fd | none |
| 438 | `pidfd_getfd` | kernel-owned missing:unclassified | package |
| 439 | `faccessat2` | implemented:fd | none |
| 440 | `process_madvise` | kernel-owned missing:unclassified | package |
| 441 | `epoll_pwait2` | kernel-owned missing:unclassified | package |
| 442 | `mount_setattr` | implemented:mount | none |
| 443 | `quotactl_fd` | kernel-owned missing:unclassified | package |
| 444 | `landlock_create_ruleset` | kernel-owned missing:unclassified | package |
| 445 | `landlock_add_rule` | kernel-owned missing:unclassified | package |
| 446 | `landlock_restrict_self` | kernel-owned missing:unclassified | package |
| 447 | `memfd_secret` | kernel-owned missing:unclassified | package |
| 448 | `process_mrelease` | kernel-owned missing:unclassified | package |
| 449 | `futex_waitv` | kernel-owned missing:unclassified | package |
| 450 | `set_mempolicy_home_node` | kernel-owned missing:unclassified | package |
| 451 | `cachestat` | kernel-owned missing:unclassified | package |
| 452 | `fchmodat2` | implemented:fd | none |
| 453 | `map_shadow_stack` | kernel-owned missing:unclassified | package |
| 454 | `futex_wake` | kernel-owned missing:unclassified | package |
| 455 | `futex_wait` | kernel-owned missing:unclassified | package |
| 456 | `futex_requeue` | kernel-owned missing:unclassified | package |
| 457 | `statmount` | implemented:mount | none |
| 458 | `listmount` | implemented:mount | none |
| 459 | `lsm_get_self_attr` | kernel-owned missing:unclassified | package |
| 460 | `lsm_set_self_attr` | kernel-owned missing:unclassified | package |
| 461 | `lsm_list_modules` | kernel-owned missing:unclassified | package |
| 462 | `mseal` | kernel-owned missing:unclassified | package |
