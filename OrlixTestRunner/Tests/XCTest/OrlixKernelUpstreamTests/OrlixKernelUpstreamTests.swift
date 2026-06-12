import XCTest
@testable import OrlixTestRunner

final class OrlixKernelUpstreamTests: XCTestCase {
    func testMountNamespaceProbeVerifiesMountinfoThroughOrlixOSTerminalSession()
        throws
    {
        let output = try OrlixUpstreamXCTest.run(.kernelMountNamespace)

        XCTAssertTrue(output.contains("mount_namespace_probe"))
        XCTAssertTrue(output.contains("mount namespace child verified mountinfo"))
        XCTAssertTrue(output.contains("child tmpfs mount is hidden from parent"))
        XCTAssertFalse(output.contains("clone_thread_probe"))
    }

    func testEnvironmentEntryProbeCompletesThroughOrlixOSTerminalSession()
        throws
    {
        let output = try OrlixUpstreamXCTest.run(.kernelEnvironmentEntry)

        XCTAssertTrue(output.contains("environment_entry_probe"))
        XCTAssertTrue(output.contains("environment entry parent marker created"))
        XCTAssertTrue(output.contains("environment entry child started"))
        XCTAssertTrue(output.contains("environment entry child root entered"))
        XCTAssertTrue(output.contains("environment entry child exited cleanly"))
        XCTAssertTrue(output.contains("environment entry root is hidden from parent"))
        XCTAssertTrue(output.contains("environment entry parent marker cleaned"))
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testInitExecProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelInitExec)

        XCTAssertTrue(output.contains("init_exec_probe"))
        XCTAssertTrue(output.contains("ORLIX-INIT-EXEC-PROBE"))
        XCTAssertTrue(output.contains("fork creates a child task"))
        XCTAssertTrue(output.contains("waitpid returns the forked child"))
        XCTAssertTrue(output.contains("waitpid observes the child exit status"))
        XCTAssertTrue(output.contains("mmap syscall returns writable memory"))
        XCTAssertTrue(
            output.contains(
                "writable anonymous mmap stays inside hosted user window"
            )
        )
        XCTAssertTrue(output.contains("forked child execs current image and exits"))
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testFDExecProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelFDExec)

        XCTAssertTrue(output.contains("fd_exec_probe"))
        XCTAssertTrue(output.contains("ORLIX-FD-EXEC-PROBE"))
        XCTAssertTrue(output.contains("pipe creates descriptor pairs"))
        XCTAssertTrue(
            output.contains(
                "fcntl reports descriptor without close-on-exec"
            )
        )
        XCTAssertTrue(
            output.contains("fcntl marks selected descriptor close-on-exec")
        )
        XCTAssertTrue(output.contains("ORLIX-FD-EXEC-CHILD"))
        XCTAssertTrue(output.contains("ORLIX-FD-INHERITED-READ-OK"))
        XCTAssertTrue(output.contains("ORLIX-FD-CLOEXEC-EBADF-OK"))
        XCTAssertTrue(
            output.contains("exec preserves non-close-on-exec descriptor")
        )
        XCTAssertTrue(output.contains("exec closes close-on-exec descriptor"))
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testFDAliasProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelFDAlias)

        XCTAssertTrue(output.contains("fd_alias_probe"))
        XCTAssertTrue(output.contains("ORLIX-FD-ALIAS-PROBE"))
        XCTAssertTrue(output.contains("/dev/fd is a directory"))
        XCTAssertTrue(
            output.contains("/dev/fd opens the referenced descriptor path")
        )
        XCTAssertTrue(output.contains("/dev/stdin aliases fd 0"))
        XCTAssertTrue(output.contains("/dev/stdout aliases fd 1"))
        XCTAssertTrue(output.contains("/dev/stderr aliases fd 2"))
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testSignalWaitProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelSignalWait)

        XCTAssertTrue(output.contains("signal_wait_probe"))
        XCTAssertTrue(output.contains("ORLIX-SIGNAL-WAIT-PROBE"))
        XCTAssertTrue(
            output.contains("signal handler runs for delivered signal")
        )
        XCTAssertTrue(output.contains("blocked signal remains pending"))
        XCTAssertTrue(
            output.contains("unblocked pending signal runs handler")
        )
        XCTAssertTrue(
            output.contains("waitpid observes signal termination status")
        )
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testPipePollProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelPipePoll)

        XCTAssertTrue(output.contains("pipe_poll_probe"))
        XCTAssertTrue(output.contains("ORLIX-PIPE-POLL-PROBE"))
        XCTAssertTrue(output.contains("pipe creates nonblocking read descriptor"))
        XCTAssertTrue(
            output.contains("empty nonblocking pipe read returns EAGAIN")
        )
        XCTAssertTrue(output.contains("empty pipe read poll times out"))
        XCTAssertTrue(output.contains("pipe write end polls writable"))
        XCTAssertTrue(
            output.contains("pipe read end polls readable after write")
        )
        XCTAssertTrue(output.contains("pipe read returns written payload"))
        XCTAssertTrue(
            output.contains("pipe read end polls hangup after writer closes")
        )
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testPipeSelectProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelPipeSelect)

        XCTAssertTrue(output.contains("pipe_select_probe"))
        XCTAssertTrue(output.contains("ORLIX-PIPE-SELECT-PROBE"))
        XCTAssertTrue(
            output.contains(
                "pipe creates nonblocking read descriptor for select"
            )
        )
        XCTAssertTrue(
            output.contains(
                "empty nonblocking pipe read returns EAGAIN before select"
            )
        )
        XCTAssertTrue(output.contains("empty pipe read select times out"))
        XCTAssertTrue(output.contains("pipe write end selects writable"))
        XCTAssertTrue(
            output.contains("pipe read end selects readable after write")
        )
        XCTAssertTrue(output.contains("pipe read returns selected payload"))
        XCTAssertTrue(
            output.contains(
                "pipe read end selects readable after writer closes"
            )
        )
        XCTAssertTrue(
            output.contains("pipe read returns EOF after selected writer close")
        )
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testPipeEpollProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelPipeEpoll)

        XCTAssertTrue(output.contains("pipe_epoll_probe"))
        XCTAssertTrue(output.contains("ORLIX-PIPE-EPOLL-PROBE"))
        XCTAssertTrue(
            output.contains(
                "pipe creates nonblocking read descriptor for epoll"
            )
        )
        XCTAssertTrue(output.contains("epoll_create1 returns epoll descriptor"))
        XCTAssertTrue(
            output.contains(
                "empty nonblocking pipe read returns EAGAIN before epoll"
            )
        )
        XCTAssertTrue(output.contains("epoll_ctl adds pipe read end"))
        XCTAssertTrue(output.contains("empty pipe read epoll times out"))
        XCTAssertTrue(output.contains("pipe write end epolls writable"))
        XCTAssertTrue(
            output.contains("pipe read end epolls readable after write")
        )
        XCTAssertTrue(output.contains("pipe read returns epoll payload"))
        XCTAssertTrue(
            output.contains("pipe read end epolls hangup after writer closes")
        )
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testPseudoFSProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelPseudoFS)

        XCTAssertTrue(output.contains("pseudo_fs_probe"))
        XCTAssertTrue(output.contains("ORLIX-PSEUDO-FS-PROBE"))
        XCTAssertTrue(output.contains("mountinfo exposes procfs at /proc"))
        XCTAssertTrue(output.contains("mountinfo exposes sysfs at /sys"))
        XCTAssertTrue(output.contains("mountinfo exposes devtmpfs at /dev"))
        XCTAssertTrue(output.contains("mountinfo exposes devpts at /dev/pts"))
        XCTAssertTrue(output.contains("mountinfo exposes tmpfs at /tmp"))
        XCTAssertTrue(
            output.contains("proc self status fd and mounts are readable")
        )
        XCTAssertTrue(
            output.contains("core dev nodes are Linux character devices")
        )
        XCTAssertTrue(output.contains("devpts mountpoint is a directory"))
        XCTAssertTrue(
            output.contains("devpts allocates a PTY master through ptmx")
        )
        XCTAssertTrue(
            output.contains("sysfs exposes virtio device directory")
        )
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testPathErrnoProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelPathErrno)

        XCTAssertTrue(output.contains("path_errno_probe"))
        XCTAssertTrue(
            output.contains("path errno fixture created through Linux VFS")
        )
        XCTAssertTrue(output.contains("ORLIX-ORACLE-BEGIN path-errno"))
        XCTAssertTrue(
            output.contains(
                #"{"operation":"open","path":"missing","errno":2"#
            )
        )
        XCTAssertTrue(output.contains("missing path returns ENOENT"))
        XCTAssertTrue(
            output.contains(
                #"{"operation":"open","path":"regular/child","errno":20"#
            )
        )
        XCTAssertTrue(output.contains("non-directory child returns ENOTDIR"))
        XCTAssertTrue(
            output.contains(
                #"{"operation":"stat","path":"loop-a","errno":40"#
            )
        )
        XCTAssertTrue(output.contains("symlink loop returns ELOOP"))
        XCTAssertTrue(
            output.contains(
                #"{"operation":"stat","path":"regular/","errno":20"#
            )
        )
        XCTAssertTrue(
            output.contains("trailing slash on regular file returns ENOTDIR")
        )
        XCTAssertTrue(output.contains("ORLIX-ORACLE-END path-errno"))
        XCTAssertTrue(output.contains("path errno fixture cleaned"))
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testCloneThreadProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelCloneThread)

        XCTAssertTrue(output.contains("clone_thread_probe"))
        XCTAssertTrue(
            output.contains(
                "mlibc-shaped clone thread stack runs through Linux clone"
            )
        )
        XCTAssertTrue(
            output.contains(
                "mlibc-shaped clone TLS and futex join handshake completes"
            )
        )
        XCTAssertTrue(
            output.contains(
                "mlibc-shaped clone stack supports deep alloca faults"
            )
        )
        XCTAssertFalse(output.contains("# exec /orlix/mount_namespace_probe"))
    }

    func testBootProfileContractVerifiesVirtioConsoleThroughOrlixOSTerminalSession()
        throws
    {
        let output = try OrlixUpstreamXCTest.run(.kernelBootProfile)

        XCTAssertTrue(output.contains("boot_profile_contract"))
        XCTAssertTrue(output.contains("cmdline selects the Orlix virtio console"))
        XCTAssertTrue(output.contains("live consoles include the Orlix virtio console"))
        XCTAssertTrue(
            output.contains(
                "live device tree labels vda as immutable base storage"
            )
        )
        XCTAssertTrue(
            output.contains(
                "live device tree labels vdb as writable state storage"
            )
        )
        XCTAssertTrue(output.contains("Linux exposes the immutable base block device"))
        XCTAssertTrue(output.contains("Linux exposes the writable state block device"))
        XCTAssertFalse(output.contains("# exec /orlix/clone_thread_probe"))
    }

    func testRandomDeviceProbeCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernelRandomDevice)

        XCTAssertTrue(output.contains("random_device_probe"))
        XCTAssertTrue(output.contains("Linux getrandom returns random bytes"))
        XCTAssertTrue(output.contains("Linux /dev/urandom returns random bytes"))
        XCTAssertFalse(output.contains("# exec /orlix/clone_thread_probe"))
    }

    func testVirtioBlockEnvironmentProbeCompletesThroughOrlixOSTerminalSession()
        throws
    {
        let output = try OrlixUpstreamXCTest.run(.kernelVirtioBlockEnvironment)

        XCTAssertTrue(output.contains("virtio_blk_environment_probe"))
        XCTAssertTrue(
            output.contains(
                "immutable base root is visible as /dev/vda block device"
            )
        )
        XCTAssertTrue(
            output.contains(
                "writable state root is visible as /dev/vdb block device"
            )
        )
        XCTAssertTrue(output.contains("sysfs marks /dev/vda read-only"))
        XCTAssertTrue(output.contains("sysfs marks /dev/vdb writable"))
        XCTAssertTrue(output.contains("sysfs reports nonzero /dev/vda size"))
        XCTAssertTrue(output.contains("sysfs reports nonzero /dev/vdb size"))
        XCTAssertTrue(
            output.contains(
                "sysfs exposes /dev/vda virtio block identifier"
            )
        )
        XCTAssertTrue(
            output.contains(
                "sysfs exposes /dev/vdb virtio block identifier"
            )
        )
        XCTAssertTrue(
            output.contains(
                "/dev/vda serves sector reads through Linux block layer"
            )
        )
        XCTAssertTrue(
            output.contains(
                "/dev/vdb serves sector reads through Linux block layer"
            )
        )
        XCTAssertTrue(output.contains("/dev/vda rejects sector writes"))
        XCTAssertTrue(output.contains("/dev/vdb accepts sector writes"))
        XCTAssertTrue(output.contains("/dev/vdb flushes after sector writes"))
        XCTAssertFalse(output.contains("# exec /orlix/clone_thread_probe"))
    }

    func testEnvironmentStateWritebackProbeCompletesThroughOrlixOSTerminalSession()
        throws
    {
        let output = try OrlixUpstreamXCTest.run(.kernelEnvironmentStateWriteback)

        XCTAssertTrue(output.contains("environment_state_writeback_probe"))
        XCTAssertTrue(output.contains("writable state block mounts as ext4"))
        XCTAssertTrue(output.contains("environment state marker write succeeds"))
        XCTAssertTrue(output.contains("environment state marker sync succeeds"))
        XCTAssertTrue(output.contains("writable state block remounts after sync"))
        XCTAssertTrue(output.contains("environment state marker reread succeeds"))
        XCTAssertTrue(output.contains("immutable base block still rejects writes"))
        XCTAssertTrue(output.contains("writable state block still flushes writes"))
        XCTAssertFalse(output.contains("# exec /orlix/clone_thread_probe"))
    }

    func testKselftestRootfsCompletesThroughOrlixOSTerminalSession() throws {
        let output = try OrlixUpstreamXCTest.run(.kernel)

        XCTAssertTrue(output.contains("environment_entry_probe"))
        XCTAssertTrue(output.contains("environment entry child exited cleanly"))
        XCTAssertTrue(output.contains("mount_namespace_probe"))
        XCTAssertTrue(output.contains("mount namespace child verified mountinfo"))
        XCTAssertTrue(output.contains("child tmpfs mount is hidden from parent"))
        XCTAssertTrue(output.contains("virtio_blk_environment_probe"))
        XCTAssertTrue(output.contains("virtio_mmio_probe_contract"))
        XCTAssertTrue(output.contains("random_device_probe"))
        XCTAssertTrue(output.contains("Linux getrandom returns random bytes"))
        XCTAssertTrue(output.contains("Linux /dev/urandom returns random bytes"))
        XCTAssertTrue(
            output.contains("upstream hwrng device returns virtio-backed entropy")
        )
    }
}
