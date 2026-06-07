import XCTest
@testable import OrlixTestRunner

final class OrlixKernelUpstreamTests: XCTestCase {
    func testKselftestRootfsCompletesThroughOrlixKitTerminalSession() throws {
        try OrlixUpstreamXCTest.run(.kernel)
    }
}
