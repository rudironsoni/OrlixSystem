import XCTest
@testable import OrlixTestRunner

final class OrlixMLibCUpstreamTests: XCTestCase {
    func testMLibCRootfsCompletesThroughOrlixKitTerminalSession() throws {
        try OrlixUpstreamXCTest.run(.mlibc)
    }
}
