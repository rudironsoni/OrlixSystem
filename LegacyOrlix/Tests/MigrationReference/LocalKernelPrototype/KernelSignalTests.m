#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface KernelSignalTests : LinuxKernelDynamicTestCase
@end

@implementation KernelSignalTests

+ (const struct kunit_suite *)kunitSuite {
    return kernel_signal_suite();
}

@end
