#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface KernelFutexTests : LinuxKernelDynamicTestCase
@end

@implementation KernelFutexTests

+ (const struct kunit_suite *)kunitSuite {
    return kernel_futex_suite();
}

@end
