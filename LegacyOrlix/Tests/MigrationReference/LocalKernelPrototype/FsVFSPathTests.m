#import "LinuxKernelDynamicTestCase.h"

#include "kunit/suite_registry.h"

@interface FsVFSPathTests : LinuxKernelDynamicTestCase
@end

@implementation FsVFSPathTests

+ (const struct kunit_suite *)kunitSuite {
    return fs_vfs_path_suite();
}

@end
