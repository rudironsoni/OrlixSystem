#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface KernelTaskGroupTests : LinuxKernelDynamicTestCase
@end

@implementation KernelTaskGroupTests

+ (const struct kunit_suite *)kunitSuite {
    return kernel_task_group_suite();
}

@end
