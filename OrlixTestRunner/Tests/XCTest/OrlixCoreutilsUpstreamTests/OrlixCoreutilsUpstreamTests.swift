import XCTest
@testable import OrlixTestRunner

final class OrlixCoreutilsUpstreamTests: XCTestCase {
    func testCoreutilsRootfsCompletesThroughOrlixOSTerminalSession() throws {
        try OrlixUpstreamXCTest.run(.coreutils)
    }
}
