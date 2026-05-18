#import "LinuxKernelDynamicTestCase.h"

#import <objc/runtime.h>

#include "kunit/kunit.h"

static const void *SelectorMapKey = &SelectorMapKey;

static NSString *SanitizeCaseName(const char *name) {
    NSMutableString *sanitized = [NSMutableString stringWithString:@"test_"];
    NSString *source = name ? [NSString stringWithUTF8String:name] : @"unnamed_case";

    for (NSUInteger index = 0; index < source.length; index++) {
        unichar character = [source characterAtIndex:index];
        BOOL alphanumeric = [[NSCharacterSet alphanumericCharacterSet] characterIsMember:character];
        unichar output = alphanumeric ? character : (unichar)'_';
        [sanitized appendFormat:@"%C", output];
    }
    return sanitized;
}

@interface LinuxKernelDynamicTestCase ()

- (void)dispatchDynamicCaseForSelector:(NSString *)selectorName;

@end

static void RunDynamicKUnitCase(id self, SEL _cmd) {
    [(LinuxKernelDynamicTestCase *)self dispatchDynamicCaseForSelector:NSStringFromSelector(_cmd)];
}

static void RecordDynamicFailure(LinuxKernelDynamicTestCase *testCase, NSString *description, NSString *filePath, NSUInteger line) {
    NSString *compactDescription = description ?: @"dynamic KUnit failure";
    NSString *resolvedPath = filePath ?: @__FILE__;
    XCTSourceCodeLocation *location = [[XCTSourceCodeLocation alloc] initWithFilePath:resolvedPath
                                                                           lineNumber:(NSInteger)line];
    XCTSourceCodeContext *context = [[XCTSourceCodeContext alloc] initWithLocation:location];
    XCTIssue *issue = [[XCTIssue alloc] initWithType:XCTIssueTypeAssertionFailure
                                  compactDescription:compactDescription
                                 detailedDescription:nil
                                   sourceCodeContext:context
                                     associatedError:nil
                                         attachments:@[]];
    [testCase recordIssue:issue];
}

@implementation LinuxKernelDynamicTestCase

+ (const struct kunit_suite *)kunitSuite {
    return NULL;
}

+ (NSArray<NSInvocation *> *)testInvocations {
    const struct kunit_suite *suite = [self kunitSuite];
    NSMutableArray<NSInvocation *> *invocations = [NSMutableArray array];
    NSMutableDictionary<NSString *, NSNumber *> *selectorMap = [NSMutableDictionary dictionary];

    if (!suite) {
        return invocations;
    }

    objc_setAssociatedObject(self, SelectorMapKey, selectorMap, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    for (size_t index = 0; index < suite->case_count; index++) {
        NSString *selectorName = SanitizeCaseName(suite->cases[index].name);
        SEL selector = NSSelectorFromString(selectorName);
        NSMethodSignature *signature;
        NSInvocation *invocation;

        selectorMap[selectorName] = @(index);
        if (!class_getInstanceMethod(self, selector)) {
            class_addMethod(self, selector, (IMP)RunDynamicKUnitCase, "v@:");
        }

        signature = [self instanceMethodSignatureForSelector:selector];
        invocation = [NSInvocation invocationWithMethodSignature:signature];
        invocation.selector = selector;
        [invocation retainArguments];
        [invocations addObject:invocation];
    }

    return invocations;
}

- (void)dispatchDynamicCaseForSelector:(NSString *)selectorName {
    const struct kunit_suite *suite = [[self class] kunitSuite];
    NSDictionary<NSString *, NSNumber *> *selectorMap = objc_getAssociatedObject([[self class] class], SelectorMapKey);
    NSNumber *caseIndex = selectorMap[selectorName];
    struct kunit test;

    if (!suite || !caseIndex) {
        RecordDynamicFailure(self,
                             @"dynamic KUnit case mapping was not initialized",
                             @__FILE__,
                             __LINE__);
        return;
    }

    kunit_init(&test, suite->name, suite->cases[caseIndex.unsignedIntegerValue].name);
    if (suite->init) {
        suite->init(&test);
    }
    suite->cases[caseIndex.unsignedIntegerValue].run_case(&test);
    if (suite->exit) {
        suite->exit(&test);
    }
    if (!test.failed) {
        return;
    }

    RecordDynamicFailure(self,
                         [NSString stringWithUTF8String:test.failure_message],
                         [NSString stringWithUTF8String:test.failure_file],
                         test.failure_line);
}

@end
