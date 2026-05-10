#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface FsPTYJobControlTests : LinuxKernelDynamicTestCase
@end

@implementation FsPTYJobControlTests

+ (const struct kunit_suite *)kunitSuite {
    return fs_pty_job_control_suite();
}

@end
