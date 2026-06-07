#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

@interface OrlixHostAdapterTests : XCTestCase
@end

@implementation OrlixHostAdapterTests

- (void)testHostedAppHasPrivateFrameworksDirectory
{
    NSURL *frameworksURL = [NSBundle mainBundle].privateFrameworksURL;

    XCTAssertNotNil(frameworksURL);
}

@end
