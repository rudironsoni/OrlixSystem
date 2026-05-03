#import <XCTest/XCTest.h>

#include <errno.h>

#include "kernel/init.h"

extern int kernel_init_contract_start_kernel_creates_current_init_task(void);
extern int kernel_init_contract_init_task_identity_is_linux_shaped(void);
extern int kernel_init_contract_init_task_cwd_and_root_are_slash(void);
extern int kernel_init_contract_kernel_boot_exposes_root(void);
extern int kernel_init_contract_kernel_boot_exposes_etc_passwd(void);
extern int kernel_init_contract_kernel_boot_exposes_dev_root(void);
extern int kernel_init_contract_kernel_boot_exposes_proc_root(void);
extern int kernel_init_contract_kernel_boot_exposes_sys_root_or_documents_policy(void);
extern int kernel_init_contract_kernel_boot_exposes_tmp_and_var_cache_routes(void);
extern int kernel_init_contract_kernel_boot_stdio_policy_is_explicit(void);
extern int kernel_init_contract_proc_self_reflects_current_task(void);
extern int kernel_init_contract_proc_self_fd_reflects_boot_descriptors(void);
extern int kernel_init_contract_proc_self_fdinfo_reflects_boot_descriptors(void);
extern int kernel_init_contract_proc_self_exe_policy_before_exec_is_explicit(void);
extern int kernel_init_contract_kernel_shutdown_and_reboot_restores_init_state(void);
extern int kernel_init_contract_exec_preferred_init_launches_pid1(void);
extern int kernel_init_contract_exec_init_search_uses_first_existing_candidate(void);
extern int kernel_init_contract_exec_init_returns_enoent_when_no_candidate_exists(void);
extern int kernel_init_contract_exec_init_preserves_pid1_identity(void);
extern int kernel_init_contract_exec_init_updates_proc_self_exe_cmdline_comm(void);
extern int kernel_init_contract_exec_init_closes_cloexec_only(void);
extern int kernel_init_contract_pid1_adopts_orphaned_children(void);
extern int kernel_init_contract_exec_script_init_uses_interpreter(void);

@interface KernelInitTests : XCTestCase
@end

@implementation KernelInitTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before LinuxKernel init tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before LinuxKernel init tests");
}

- (void)testStartKernelCreatesCurrentInitTask {
    XCTAssertEqual(kernel_init_contract_start_kernel_creates_current_init_task(), 0, @"errno %d", errno);
}

- (void)testInitTaskIdentityIsLinuxShaped {
    XCTAssertEqual(kernel_init_contract_init_task_identity_is_linux_shaped(), 0, @"errno %d", errno);
}

- (void)testInitTaskCwdAndRootAreSlash {
    XCTAssertEqual(kernel_init_contract_init_task_cwd_and_root_are_slash(), 0, @"errno %d", errno);
}

- (void)testKernelBootExposesRoot {
    XCTAssertEqual(kernel_init_contract_kernel_boot_exposes_root(), 0, @"errno %d", errno);
}

- (void)testKernelBootExposesEtcPasswd {
    XCTAssertEqual(kernel_init_contract_kernel_boot_exposes_etc_passwd(), 0, @"errno %d", errno);
}

- (void)testKernelBootExposesDevRoot {
    XCTAssertEqual(kernel_init_contract_kernel_boot_exposes_dev_root(), 0, @"errno %d", errno);
}

- (void)testKernelBootExposesProcRoot {
    XCTAssertEqual(kernel_init_contract_kernel_boot_exposes_proc_root(), 0, @"errno %d", errno);
}

- (void)testKernelBootExposesSysRoot {
    XCTAssertEqual(kernel_init_contract_kernel_boot_exposes_sys_root_or_documents_policy(), 0, @"errno %d", errno);
}

- (void)testKernelBootExposesTmpAndVarCacheRoutes {
    XCTAssertEqual(kernel_init_contract_kernel_boot_exposes_tmp_and_var_cache_routes(), 0, @"errno %d", errno);
}

- (void)testKernelBootStdioPolicyIsExplicit {
    XCTAssertEqual(kernel_init_contract_kernel_boot_stdio_policy_is_explicit(), 0, @"errno %d", errno);
}

- (void)testProcSelfReflectsCurrentTask {
    XCTAssertEqual(kernel_init_contract_proc_self_reflects_current_task(), 0, @"errno %d", errno);
}

- (void)testProcSelfFdReflectsBootDescriptors {
    XCTAssertEqual(kernel_init_contract_proc_self_fd_reflects_boot_descriptors(), 0, @"errno %d", errno);
}

- (void)testProcSelfFdinfoReflectsBootDescriptors {
    XCTAssertEqual(kernel_init_contract_proc_self_fdinfo_reflects_boot_descriptors(), 0, @"errno %d", errno);
}

- (void)testProcSelfExePolicyBeforeExecIsExplicit {
    XCTAssertEqual(kernel_init_contract_proc_self_exe_policy_before_exec_is_explicit(), 0, @"errno %d", errno);
}

- (void)testKernelShutdownAndRebootRestoresInitState {
    XCTAssertEqual(kernel_init_contract_kernel_shutdown_and_reboot_restores_init_state(), 0, @"errno %d", errno);
}

- (void)testExecPreferredInitLaunchesPid1 {
    XCTAssertEqual(kernel_init_contract_exec_preferred_init_launches_pid1(), 0, @"errno %d", errno);
}

- (void)testExecInitSearchUsesFirstExistingCandidate {
    XCTAssertEqual(kernel_init_contract_exec_init_search_uses_first_existing_candidate(), 0, @"errno %d", errno);
}

- (void)testExecInitReturnsEnoentWhenNoCandidateExists {
    XCTAssertEqual(kernel_init_contract_exec_init_returns_enoent_when_no_candidate_exists(), 0, @"errno %d", errno);
}

- (void)testExecInitPreservesPid1Identity {
    XCTAssertEqual(kernel_init_contract_exec_init_preserves_pid1_identity(), 0, @"errno %d", errno);
}

- (void)testExecInitUpdatesProcSelfExeCmdlineComm {
    XCTAssertEqual(kernel_init_contract_exec_init_updates_proc_self_exe_cmdline_comm(), 0, @"errno %d", errno);
}

- (void)testExecInitClosesCloexecOnly {
    XCTAssertEqual(kernel_init_contract_exec_init_closes_cloexec_only(), 0, @"errno %d", errno);
}

- (void)testPid1AdoptsOrphanedChildren {
    XCTAssertEqual(kernel_init_contract_pid1_adopts_orphaned_children(), 0, @"errno %d", errno);
}

- (void)testExecScriptInitUsesInterpreter {
    XCTAssertEqual(kernel_init_contract_exec_script_init_uses_interpreter(), 0, @"errno %d", errno);
}

@end
