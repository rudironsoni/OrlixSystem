import XCTest
@testable import OrlixTestRunner

enum OrlixUpstreamXCTest {
    static func run(_ spec: OrlixUpstreamTestRunSpec) throws {
        let output = try OrlixUpstreamTestSessionRunner(spec: spec).run()
        XCTAssertFalse(output.isEmpty)
    }
}
