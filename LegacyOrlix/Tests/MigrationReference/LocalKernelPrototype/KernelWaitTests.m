#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface KernelWaitTests : LinuxKernelDynamicTestCase
@end

@implementation KernelWaitTests

+ (const struct kunit_suite *)kunitSuite {
    return kernel_wait_suite();
}

@end
