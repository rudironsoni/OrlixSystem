import XCTest
@testable import OrlixTestRunner

enum OrlixUpstreamXCTest {
    @discardableResult
    static func run(_ spec: OrlixUpstreamTestRunSpec) throws -> String {
        let output = try OrlixUpstreamTestSessionRunner(spec: spec).run()
        XCTAssertFalse(output.isEmpty)
        return output
    }
}
