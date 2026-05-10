#ifdef __OBJC__
#if __has_include(<XCTest/XCTest.h>)
#import <XCTest/XCTest.h>
#else
__attribute__((objc_root_class))
@interface XCTestCase
@end
#endif

struct kunit_suite;

@interface LinuxKernelDynamicTestCase : XCTestCase

+ (const struct kunit_suite *)kunitSuite;

@end
#endif
