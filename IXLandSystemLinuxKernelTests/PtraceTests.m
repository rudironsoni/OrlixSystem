#import <XCTest/XCTest.h>

#include <errno.h>

#include "IXLandSystemLinuxKernelTests/PtraceContract.h"
#include "kernel/cred_internal.h"
#include "kernel/init.h"

@interface PtraceTests : XCTestCase
@end

@implementation PtraceTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0);
    cred_reset_to_defaults();
}

- (void)testAttachDetachChildSameUserNamespace {
    XCTAssertEqual(ptrace_contract_attach_detach_child_same_user_namespace(), 0, @"errno %d", errno);
}

- (void)testNewuserChildCannotAttachParentNamespaceTask {
    XCTAssertEqual(ptrace_contract_newuser_child_cannot_attach_parent_namespace_task(), 0, @"errno %d", errno);
}

- (void)testNewuserChildCanAttachSameNamespaceTask {
    XCTAssertEqual(ptrace_contract_newuser_child_can_attach_same_namespace_task(), 0, @"errno %d", errno);
}

@end
