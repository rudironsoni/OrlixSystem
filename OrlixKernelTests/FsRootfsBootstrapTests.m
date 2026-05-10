#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface FsRootfsBootstrapTests : LinuxKernelDynamicTestCase
@end

@implementation FsRootfsBootstrapTests

+ (const struct kunit_suite *)kunitSuite {
    return fs_rootfs_bootstrap_suite();
}

@end
