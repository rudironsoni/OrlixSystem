#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface FsPipeTests : LinuxKernelDynamicTestCase
@end

@implementation FsPipeTests

+ (const struct kunit_suite *)kunitSuite {
    return fs_pipe_suite();
}

@end
