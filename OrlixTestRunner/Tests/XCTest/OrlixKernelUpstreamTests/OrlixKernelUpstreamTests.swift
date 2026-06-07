import XCTest
@testable import OrlixTestRunner

final class OrlixKernelUpstreamTests: XCTestCase {
    func testKselftestRootfsCompletesThroughOrlixOSTerminalSession() throws {
        try OrlixUpstreamXCTest.run(.kernel)
    }
}
