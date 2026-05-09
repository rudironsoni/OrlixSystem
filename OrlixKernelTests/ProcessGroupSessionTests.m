#import <XCTest/XCTest.h>

#include <errno.h>
#include <string.h>

#include "kernel/init.h"
#include "kernel/task.h"
#include "ProcessGroupSessionContract.h"

static void reset_process_group_session_test_kernel_state(void) {
    struct task_struct *child;

    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before process-group/session tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before process-group/session tests");
    XCTAssertNotEqual(init_task, NULL, @"init_task must exist before process-group/session tests");

    if (!init_task) {
        return;
    }

    set_current(init_task);
    init_task->parent = NULL;
    init_task->ppid = 0;
    init_task->pgid = init_task->pid;
    init_task->sid = init_task->pid;
    atomic_store(&init_task->execed, false);

    while ((child = init_task->children) != NULL) {
        task_unlink_child_impl(init_task, child);
        child->parent = NULL;
        child->ppid = 0;
    }

    set_current(init_task);
}

@interface ProcessGroupSessionTests : XCTestCase
@end

@implementation ProcessGroupSessionTests

- (void)setUp {
    [super setUp];
    reset_process_group_session_test_kernel_state();
}

- (void)tearDown {
    reset_process_group_session_test_kernel_state();
    [super tearDown];
}

- (void)testPublicGroupAndSessionIdentityMatchesInitTask {
    XCTAssertEqual(process_group_session_contract_public_group_and_session_identity_matches_init_task(), 0,
                   @"errno %d", errno);
}

- (void)testPublicGetpgidZeroMatchesCurrentGroup {
    XCTAssertEqual(process_group_session_contract_public_getpgid_zero_matches_current_group(), 0,
                   @"errno %d", errno);
}

- (void)testPublicSetpgidMovesChildIntoOwnGroup {
    XCTAssertEqual(process_group_session_contract_public_setpgid_moves_child_into_own_group(), 0,
                   @"errno %d", errno);
}

- (void)testPublicSetpgidRejectsExecedChild {
    XCTAssertEqual(process_group_session_contract_public_setpgid_rejects_execed_child(), 0,
                   @"errno %d", errno);
}

- (void)testPublicSetsidCreatesNewSessionForNonLeader {
    XCTAssertEqual(process_group_session_contract_public_setsid_creates_new_session_for_non_leader(), 0,
                   @"errno %d", errno);
}

- (void)testPublicSetpgidRejectsSessionLeader {
    XCTAssertEqual(process_group_session_contract_public_setpgid_rejects_session_leader(), 0,
                   @"errno %d", errno);
}

@end
