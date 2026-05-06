#import <XCTest/XCTest.h>

#include <errno.h>

#include "SeccompContract.h"
#include "kernel/init.h"

@interface SeccompTests : XCTestCase
@end

@implementation SeccompTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0);
}

- (void)testTaskErrnoPolicyDeniesSyscallDispatch {
    XCTAssertEqual(seccomp_contract_task_errno_policy_denies_syscall_dispatch(), 0, @"errno %d", errno);
}

- (void)testUnmentionedSyscallRemainsAllowed {
    XCTAssertEqual(seccomp_contract_unmentioned_syscall_remains_allowed(), 0, @"errno %d", errno);
}

- (void)testThreadGroupPolicyAppliesToThreadPeer {
    XCTAssertEqual(seccomp_contract_thread_group_policy_applies_to_thread_peer(), 0, @"errno %d", errno);
}

@end
