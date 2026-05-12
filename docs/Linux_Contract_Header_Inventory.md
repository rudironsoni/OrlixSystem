# Linux Contract And Header Inventory

## Purpose

This document defines the Linux-facing contract and header layer required
across the full roadmap.

It is not a milestone plan.

It is the ownership inventory and replacement target for all contract and
header work before more milestone-specific behavior work lands.

## Scope

This inventory is derived from:

- `00-Native_Package_Compatibility_Roadmap.md`
- `01-OrlixKernel_OrlixHostAdapter_Split_Plan.md`
- `02-Sysroot_Build_Truth_Plan.md`
- `03-Exec_Shebang_Native_Execution_Plan.md`
- `04-Shell_Process_Signal_PTY_Plan.md`
- `05-VFS_Runtime_Environment_Plan.md`
- `06-Networking_Curl_Plan.md`
- `07-Package_Proof_Program_Plan.md`

## Core Rule

If a concept already has a Linux name, use the Linux name.

Do not create repo-local renamed Linux concepts in `OrlixKernel`.

Examples of forbidden repo-local renames:

- `kernel_timespec`
- `kernel_timeval`
- `kernel_itimerval`
- `kernel_itimerspec`
- `kernel_sigset`
- `kernel_select_compat`
- `kernel_socket_compat`
- `pty_linux_termios`
- `pty_linux_winsize`

If a concept is not Linux-visible and is truly host mechanism, it belongs under
`OrlixHostAdapter/**` behind a narrow kernel-owned declaration.

If a concept is Linux-visible but libc-owned, it belongs to the userspace libc layer.

If a concept is Linux-visible and kernel-owned, it belongs to `OrlixKernel`
under Linux naming.

## Ownership Classes

### 1. Vendored Linux UAPI Truth

Owner:

- `third_party/linux/6.12/arm64/uapi/include/**`

Use for:

- Linux syscall numbers
- Linux UAPI constants
- Linux-visible ioctl payloads
- Linux-visible socket, signal, poll, time, and filesystem ABI contracts
- Linux-visible structs already present in vendored UAPI

Examples:

- `linux/futex.h`
- `linux/poll.h`
- `linux/socket.h`
- `linux/time_types.h`
- `asm/unistd.h`
- `asm/ioctls.h`

### 2. Linux Kernel-Internal Reference Only

Owner:

- `third_party/linux/6.12/arm64/kheaders/source/**`
- `third_party/linux/6.12/arm64/kheaders/generated/**`

Use for:

- kernel-internal source reference only
- compile-smoke and classification proof

Not package-facing ABI.

### 3. Package-Facing Libc ABI

Owner:

- package-facing libc headers

Use for:

- package-facing libc headers
- libc typedef ownership
- package sysroot installation shape

Required package-facing surfaces across milestones:

- `fcntl.h`
- `poll.h`
- `signal.h`
- `sys/select.h`
- `sys/socket.h`
- `sys/stat.h`
- `sys/time.h`
- `sys/types.h`
- `sys/uio.h`
- `time.h`
- `unistd.h`

Likely additional libc-facing owners as milestones advance:

- `termios.h`
- `sys/wait.h`
- `sys/ioctl.h`
- `sys/resource.h`
- `sys/utsname.h`
- `sys/mount.h`
- `sys/epoll.h`

### 4. OrlixKernel Linux-Owner Contracts

Owner:

- `OrlixKernel/fs/**`
- `OrlixKernel/kernel/**`
- `OrlixKernel/runtime/**`
- `OrlixKernel/include/**`
- `OrlixKernel/internal/**` only for kernel-owned private contracts,
  not for repo-local renamed Linux concepts

Use for:

- syscall-visible Linux semantics
- virtual kernel state
- internal kernel subsystem contracts
- narrow kernel-owned host mediation seams

### 5. OrlixHostAdapter Mechanism Contracts

Owner:

- `OrlixHostAdapter/**`

Use for:

- Darwin/iOS mechanism only
- storage discovery
- directory iteration
- backing fd/path mechanics
- clocks, sleeping, synchronization primitives, and thread creation as host
  realization only

Must not own Linux naming truth.

## Required Cross-Milestone Contract Surfaces

### Time And Timers

Required Linux contract surfaces:

- `struct __kernel_timespec`
- `struct __kernel_old_timeval`
- `struct __kernel_old_itimerval`
- Linux clock ids
- Linux itimer ids
- timerfd UAPI contracts

Required owners:

- vendored Linux UAPI for ABI shape
- userspace libc layer for package-facing `time.h` and `sys/time.h`
- `OrlixKernel` for Linux semantics only

Current violating repo-local surfaces:

- `OrlixKernel/kernel/time_internal.h`
- `OrlixKernel/kernel/time.c`

Replacement target:

- `OrlixKernel` code should use vendored Linux names for Linux time contracts
- host clock and sleep mediation should stay narrow under `OrlixHostAdapter`
- no repo-local `kernel_*time*` types

### Signal, Sigset, And Restart

Required Linux contract surfaces:

- Linux signal numbers
- `sigset_t`-shaped user ABI at libc boundary
- Linux `sigaction` and `sigaltstack` payloads
- Linux restart semantics

Required owners:

- vendored UAPI for signal numbers and ABI payload truth
- userspace libc layer for libc-facing signal headers
- `OrlixKernel` for signal semantics and internal state

Current violating repo-local surfaces:

- `OrlixKernel/internal/mutex.h`
- `OrlixKernel/internal/kthread.h`
- `OrlixKernel/internal/timekeeping.h`
- `OrlixKernel/kernel/signal.h`

Replacement target:

- keep internal signal state in subsystem-owned plain structs
- do not rename Linux signal concepts into `kernel_sig*`
- keep host thread-sigmask mechanics as host-only seam, not Linux concept truth

### Sockets, Message Headers, And Readiness

Required Linux contract surfaces:

- `struct sockaddr`
- `struct sockaddr_storage`
- `struct msghdr`
- `struct cmsghdr`
- socket domain/type/option constants
- `struct pollfd`
- `fd_set`

Required owners:

- vendored UAPI for Linux ABI shape
- userspace libc layer for package-facing `sys/socket.h`, `poll.h`, `sys/select.h`
- `OrlixKernel` for socket, poll, and select semantics

Current violating repo-local surfaces:

- `OrlixKernel/fs/poll.h`
- `OrlixKernel/fs/poll.c`

Replacement target:

- Linux-facing contracts use Linux names and Linux-owned structs
- internal select state uses a plain private bitset model if needed, not a fake
  `fd_set` substitute with repo-local naming
- no repo-local `*_compat` headers for Linux socket/select concepts

### PTY, TTY, Termios, And Winsize

Required Linux contract surfaces:

- Linux `termios`
- Linux `winsize`
- PTY ioctls and job-control payloads

Required owners:

- vendored UAPI for ioctl numbers and ABI payloads
- userspace libc layer for package-facing `termios.h` and ioctl-facing libc headers
- `OrlixKernel` for PTY, controlling TTY, and job-control semantics

Current violating repo-local surfaces:

- `OrlixKernel/fs/pty.h`

Replacement target:

- use Linux names for `termios` and `winsize` concepts
- keep PTY subsystem declarations Linux-shaped or plain subsystem-private
- do not prefix Linux tty concepts with `pty_linux_`

### VFS, Stat, Statfs, Dirent, And Paths

Required Linux contract surfaces:

- `struct stat`
- `struct statfs`
- `struct dirent64` / getdents payloads
- mount flags and mount payload contracts
- `openat` / `*at` path rules

Required owners:

- vendored UAPI where Linux provides the userspace contract
- userspace libc layer for package-facing stat-related libc headers
- `OrlixKernel` for VFS and path semantics

Current violating repo-local surfaces:

- `OrlixKernel/fs/stat_types.h`
- `OrlixKernel/fs/vfs.h`
- `OrlixKernel/fs/readdir.c`
- `OrlixKernel/kernel/resource.h`

Replacement target:

- stop hand-owning Linux-shaped `linux_*` structs when a real owner exists
- where pure kernel-private state is necessary, use plain subsystem nouns and
  keep them separate from userspace ABI

### Task, Wait, Exec, Procfs, And Sessions

Required Linux contract surfaces:

- clone/fork/wait/exec-visible task state
- process-group/session ids
- `wait4`, `waitid`, `waitpid`
- proc-visible argv/env/session/process-group views

Required owners:

- `OrlixKernel` for semantics
- userspace libc layer for package-facing wait/resource headers

Current violating contract-adjacent surfaces:

- `OrlixKernel/fs/exec.c` still includes Darwin process-environment header
- `OrlixKernel/internal/current.h` still exposes a current-task control seam that
  contract name

Replacement target:

- no Darwin process-environment ownership in Linux-owner exec contracts
- narrow current-task access should use a plain task subsystem declaration

### Synchronization And Thread Realization

Required Linux contract surfaces:

- futex-visible semantics
- wait queues
- task sleep/wake semantics

Host-only required mechanism surfaces:

- mutex
- condvar
- thread creation and detach
- once
- sleep
- wall/monotonic clock reads

Current violating repo-local surfaces:

- `OrlixKernel/internal/mutex.h`
- `OrlixKernel/internal/kthread.h`
- `OrlixKernel/internal/timekeeping.h`

Replacement target:

- this header should become a plain host-mechanism contract with no Linux
  concept renames
- Linux concepts such as signal sets and timespecs must not be renamed here

## Current Violation Inventory

The current repo-level contract/header violations visible across milestones are:

1. repo-local renamed Linux concepts in kernel-private headers
2. repo-local `*_compat` headers that model Linux-visible socket/select/time
   contracts
3. repo-local `linux_*` structs in VFS/stat/resource headers that may be
   standing in for real owners
4. Darwin host headers still present in Linux-owner wrapper files
5. role-labeled contract names such as `*_bridge_contract` and
   `task_current_contract`

## Required Replacement Work

The replacement work for contract/header cleanup is:

1. classify each existing header as:
   - vendored UAPI truth
   - vendored kheaders reference only
   - userspace libc package-facing ownership
   - `OrlixKernel` Linux-owner contract
   - `OrlixHostAdapter` host mechanism contract
2. delete repo-local renamed Linux concept headers
3. replace them with:
   - Linux names when the concept is Linux-owned
   - plain subsystem-private nouns when the concept is truly private state
   - host mechanism declarations only when the concept is not Linux-visible
4. remove Darwin/public host headers from Linux-owner contract files
5. keep `make lint` red until the repo-local renamed Linux concept layer is
   gone

## Immediate Blocking Files

These files are immediate blockers because they currently encode the forbidden
contract/header pattern:

- `OrlixKernel/internal/mutex.h`
- `OrlixKernel/internal/kthread.h`
- `OrlixKernel/internal/timekeeping.h`
- `OrlixKernel/kernel/signal.h`
- `OrlixKernel/internal/current.h`
- `OrlixKernel/fs/pty.h`
- `OrlixKernel/fs/vfs.h`
- `OrlixKernel/fs/stat_types.h`
- `OrlixKernel/kernel/resource.h`

## Proof Rule For This Inventory

This inventory is not complete until:

1. `make lint` fails on repo-local renamed Linux concepts and compat headers
2. the violating contract/header surfaces are replaced
3. `make lint` is green again with Linux naming or plain subsystem-private
   naming only
4. no Linux-owner header depends on Darwin/iOS public headers for Linux
   contract truth
