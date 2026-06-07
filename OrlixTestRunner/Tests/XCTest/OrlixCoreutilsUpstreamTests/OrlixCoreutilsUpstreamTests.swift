import XCTest
@testable import OrlixTestRunner

final class OrlixCoreutilsUpstreamTests: XCTestCase {
    func testCoreutilsRootfsCompletesThroughOrlixKitTerminalSession() throws {
        try OrlixUpstreamXCTest.run(.coreutils)
    }
}
