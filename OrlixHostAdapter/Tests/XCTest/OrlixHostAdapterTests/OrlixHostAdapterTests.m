#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

#import <CoreFoundation/CoreFoundation.h>

@interface OrlixHostAdapterTests : XCTestCase
@end

@implementation OrlixHostAdapterTests

- (void)testKernelFrameworkBundleIdentifierIsAvailableToHostLookup
{
    CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("org.orlix.OrlixKernel"));

    XCTAssertNotNil((__bridge id)bundle);
}

- (void)testHostedAppHasPrivateFrameworksDirectory
{
    NSURL *frameworksURL = [NSBundle mainBundle].privateFrameworksURL;

    XCTAssertNotNil(frameworksURL);
}

@end
