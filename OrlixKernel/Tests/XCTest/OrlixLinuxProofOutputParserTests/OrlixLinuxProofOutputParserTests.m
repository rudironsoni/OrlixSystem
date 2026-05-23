#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

@interface OrlixLinuxProofOutputParserTests : XCTestCase
@end

@implementation OrlixLinuxProofOutputParserTests

- (NSString *)fixtureNamed:(NSString *)name directory:(NSString *)directory
{
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSURL *url = [bundle URLForResource:name withExtension:nil subdirectory:directory];
    XCTAssertNotNil(url);
    NSError *error = nil;
    NSString *contents = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:&error];
    XCTAssertNil(error);
    XCTAssertNotNil(contents);
    return contents;
}

- (BOOL)tapTextPasses:(NSString *)text
{
    return [text containsString:@"ok "] && ![text containsString:@"not ok"];
}

- (void)testParsesKUnitKTAPFixtureAsPassing
{
    NSString *ktap = [self fixtureNamed:@"passing.ktap" directory:@"KUnit"];

    XCTAssertTrue([ktap containsString:@"KTAP version 1"]);
    XCTAssertTrue([self tapTextPasses:ktap]);
}

- (void)testParsesKselftestTAPFixtureAsPassing
{
    NSString *tap = [self fixtureNamed:@"passing.tap" directory:@"Kselftest"];

    XCTAssertTrue([tap containsString:@"TAP version 13"]);
    XCTAssertTrue([self tapTextPasses:tap]);
}

- (void)testCombinedProofOutputKeepsStreamsAndLaneSeparate
{
    NSString *output = [self fixtureNamed:@"temporary-kselftest.log" directory:@"Kselftest"];

    XCTAssertTrue([output containsString:@"ORLIX-PROOF-LANE temporary-kselftest-kernel-interface"]);
    XCTAssertTrue([output containsString:@"ORLIX-KUNIT-BEGIN"]);
    XCTAssertTrue([output containsString:@"ORLIX-KUNIT-END"]);
    XCTAssertTrue([output containsString:@"ORLIX-KSELFTEST-BEGIN"]);
    XCTAssertTrue([output containsString:@"ORLIX-KSELFTEST-END status=0"]);
}

@end
