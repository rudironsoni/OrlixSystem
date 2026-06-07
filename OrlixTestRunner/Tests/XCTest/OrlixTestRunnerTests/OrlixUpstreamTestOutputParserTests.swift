import XCTest
@testable import OrlixTestRunner

final class OrlixUpstreamTestOutputParserTests: XCTestCase {
    private let parser = OrlixUpstreamTestOutputParser()

    func testAcceptsKselftestCompletionWithPassingTAP() throws {
        let output = """
        ORLIX-KSELFTEST-INIT
        TAP version 13
        1..2
        ok 1 procfs mounted for kselftest
        ok 2 installed Orlix kselftest list is readable
        ORLIX-KSELFTEST-END
        """

        XCTAssertNoThrow(try parser.validate(output, for: .kernel))
    }

    func testAcceptsMLibCCompletionWithPassingTAP() throws {
        let output = """
        ORLIX-MLIBC-TEST-INIT
        TAP version 13
        1..1
        ok 1 installed upstream mlibc test list is readable
        ORLIX-MLIBC-TEST-END
        """

        XCTAssertNoThrow(try parser.validate(output, for: .mlibc))
    }

    func testAcceptsCoreutilsFullSuiteOnlyWithZeroFailuresZeroSkipsAndExpectedTotal() throws {
        let output = """
        ORLIX-COREUTILS-TEST-INIT
        ORLIX-COREUTILS-TEST-END failures=0 skips=0 total=733
        """

        XCTAssertNoThrow(try parser.validate(output, for: .coreutils))
    }

    func testRejectsUpstreamFailureMarkerBeforeCompletion() {
        let output = """
        TAP version 13
        1..1
        not ok 1 waitpid returns the forked child
        ORLIX-KSELFTEST-END
        """

        XCTAssertThrowsError(try parser.validate(output, for: .kernel)) { error in
            XCTAssertEqual(
                error as? OrlixUpstreamTestRunError,
                .upstreamFailure("not ok 1 waitpid returns the forked child")
            )
        }
    }

    func testRejectsMissingCompletionMarker() {
        let output = """
        TAP version 13
        1..1
        ok 1 installed upstream mlibc test list is readable
        """

        XCTAssertThrowsError(try parser.validate(output, for: .mlibc)) { error in
            XCTAssertEqual(
                error as? OrlixUpstreamTestRunError,
                .missingCompletionMarker("ORLIX-MLIBC-TEST-END")
            )
        }
    }

    func testRejectsKernelPanicBeforeMissingMarker() {
        let output = """
        Kernel panic - not syncing: Attempted to kill init
        """

        XCTAssertThrowsError(try parser.validate(output, for: .kernel)) { error in
            XCTAssertEqual(
                error as? OrlixUpstreamTestRunError,
                .kernelPanic("Kernel panic")
            )
        }
    }

    func testRejectsOutOfMemoryBeforeMissingMarker() {
        let output = """
        Out of memory: Killed process 42
        """

        XCTAssertThrowsError(try parser.validate(output, for: .coreutils)) { error in
            XCTAssertEqual(
                error as? OrlixUpstreamTestRunError,
                .oom("Out of memory")
            )
        }
    }

    func testRejectsHostCrashReportMarkers() {
        let output = """
        Incident Identifier: 00000000-0000-0000-0000-000000000000
        Exception Type: EXC_BAD_ACCESS
        """

        XCTAssertThrowsError(try parser.validate(output, for: .kernel)) { error in
            XCTAssertEqual(
                error as? OrlixUpstreamTestRunError,
                .crashReport("Incident Identifier:")
            )
        }
    }

    func testRejectsMalformedCoreutilsCompletion() {
        let output = """
        ORLIX-COREUTILS-TEST-END failures=0 total=733
        """

        XCTAssertThrowsError(try parser.validate(output, for: .coreutils)) { error in
            XCTAssertEqual(
                error as? OrlixUpstreamTestRunError,
                .malformedCoreutilsCompletion(
                    "ORLIX-COREUTILS-TEST-END failures=0 total=733"
                )
            )
        }
    }

    func testRejectsCoreutilsFailuresSkipsOrWrongTotal() {
        let output = """
        ORLIX-COREUTILS-TEST-END failures=0 skips=1 total=733
        """

        XCTAssertThrowsError(try parser.validate(output, for: .coreutils)) { error in
            XCTAssertEqual(
                error as? OrlixUpstreamTestRunError,
                .coreutilsSummaryFailed(
                    failures: 0,
                    skips: 1,
                    total: 733,
                    expectedTotal: 733
                )
            )
        }
    }
}
