import XCTest
@testable import OrlixTerminal

final class TerminalProofDriverTests: XCTestCase {
    func testCreatesDriverOnlyForShellPackageProofArgument() {
        XCTAssertNil(TerminalProofDriver.fromArguments(["OrlixTerminal"]) { _ in })
        XCTAssertNotNil(
            TerminalProofDriver.fromArguments(
                ["OrlixTerminal", "--orlix-terminal-proof=shell-package"]
            ) { _ in }
        )
    }

    func testStartsProofWhenShellPromptArrivesAcrossConsoleChunks() {
        var sentInputs: [String] = []
        var logLines: [String] = []
        let driver = TerminalProofDriver(
            sendInput: { data in
                sentInputs.append(String(decoding: data, as: UTF8.self))
            },
            log: { line in
                logLines.append(line)
            }
        )

        driver.receive("Linux boot output\nsh-")
        driver.receive("5.3# ")

        XCTAssertEqual(logLines.first, "ORLIX-TERMINAL-PROOF-BEGIN shell-package")
        XCTAssertEqual(sentInputs.count, 1)
        XCTAssertTrue(sentInputs.first?.hasSuffix("\r") == true)
    }

    func testLogsEachShellPackageStepAndNamedEndMarker() {
        var sentInputs: [String] = []
        var logLines: [String] = []
        let driver = TerminalProofDriver(
            sendInput: { data in
                sentInputs.append(String(decoding: data, as: UTF8.self))
            },
            log: { line in
                logLines.append(line)
            }
        )
        let expectedSteps = [
            ("echo", "ORLIX_ECHO"),
            ("pwd", "PWD_OK"),
            ("cd", "CD_OK"),
            ("redirection", "REDIR_OK"),
            ("pipe", "PIPE_OK"),
            ("fork-exec-wait-status", "status:7"),
            ("signal-trap", "SIG_OK"),
            ("jq", #""orlix": true"#),
            ("curl", "Release-Date:"),
            ("zsh", "ZSH_OK"),
        ]

        driver.receive("sh-5.3# ")

        XCTAssertEqual(
            logLines,
            ["ORLIX-TERMINAL-PROOF-BEGIN shell-package"]
        )
        for (index, step) in expectedSteps.enumerated() {
            XCTAssertEqual(sentInputs.count, index + 1)
            driver.receive("\(step.1)\nsh-5.3# ")
            XCTAssertTrue(
                logLines.contains("ORLIX-TERMINAL-PROOF-OK \(step.0)")
            )
        }

        XCTAssertEqual(
            logLines.last,
            "ORLIX-TERMINAL-PROOF-END shell-package status=0"
        )
        XCTAssertEqual(sentInputs.count, expectedSteps.count)
    }

    func testMatchesJqOutputWithAnsiColorSequences() {
        var sentInputs: [String] = []
        var logLines: [String] = []
        let driver = TerminalProofDriver(
            sendInput: { data in
                sentInputs.append(String(decoding: data, as: UTF8.self))
            },
            log: { line in
                logLines.append(line)
            }
        )
        let preJqOutputs = [
            "ORLIX_ECHO",
            "PWD_OK",
            "CD_OK",
            "REDIR_OK",
            "PIPE_OK",
            "status:7",
            "SIG_OK",
        ]

        driver.receive("sh-5.3# ")
        for output in preJqOutputs {
            driver.receive("\(output)\nsh-5.3# ")
        }

        XCTAssertTrue(sentInputs.last?.contains("/usr/bin/jq") == true)
        driver.receive(
            "\u{1b}[1;39m{\r\n  \u{1b}[0m\u{1b}[1;34m\"orlix\"\u{1b}[0m\u{1b}[1;39m: \u{1b}[0m\u{1b}[0;39mtrue\u{1b}[0m\r\n\u{1b}[1;39m}\u{1b}[0m\r\nsh-5.3# "
        )

        XCTAssertTrue(logLines.contains("ORLIX-TERMINAL-PROOF-OK jq"))
        XCTAssertTrue(sentInputs.last?.contains("/usr/bin/curl --version") == true)
    }
}
