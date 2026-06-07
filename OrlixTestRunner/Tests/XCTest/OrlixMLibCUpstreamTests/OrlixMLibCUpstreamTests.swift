import XCTest
@testable import OrlixTestRunner

final class OrlixMLibCUpstreamTests: XCTestCase {
    func testMLibCRootfsCompletesThroughOrlixOSTerminalSession() throws {
        try OrlixUpstreamXCTest.run(.mlibc)
    }
}
