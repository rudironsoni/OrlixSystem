#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface KernelProcessGroupSessionTests : LinuxKernelDynamicTestCase
@end

@implementation KernelProcessGroupSessionTests

+ (const struct kunit_suite *)kunitSuite {
    return kernel_process_group_session_suite();
}

@end
