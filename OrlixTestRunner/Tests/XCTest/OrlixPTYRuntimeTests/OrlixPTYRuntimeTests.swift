@testable import OrlixOS
import Foundation
import XCTest

final class OrlixPTYRuntimeTests: XCTestCase {
    func testLinuxPTYCarriesInteractiveShellInputAndOutput() throws {
        let runner = OrlixPTYRuntimeProofRunner()
        let output = try runner.run()

        XCTAssertTrue(output.contains("ORLIX_PTY_INPUT_OK"))
        XCTAssertTrue(output.contains("ORLIX_PTY_TTY_OK"))
        XCTAssertTrue(output.contains("ORLIX_PTY_DONE"))
        XCTAssertTrue(output.contains("/dev/pts/"))
    }
}

private final class OrlixPTYRuntimeProofRunner: @unchecked Sendable {
    private static let doneMarker = "ORLIX_PTY_DONE"
    private static let firstOutputTimeout: TimeInterval = 30
    private static let timeout: TimeInterval = 600
    private static let commandScript = [
        #"printf '%s%s\n' ORLIX_PTY_ INPUT_OK"#,
        #"""
        if /bin/test -t 0 && /bin/test -t 1 && /bin/test -t 2; then printf '%s%s\n' ORLIX_PTY_ TTY_OK; else printf '%s%s\n' ORLIX_PTY_ TTY_FAIL; fi
        """#,
        #"""
        if command -v tty >/dev/null 2>&1; then tty; elif /bin/test -x /bin/tty; then /bin/tty; elif /bin/test -x /usr/bin/tty; then /usr/bin/tty; else printf '%s%s\n' ORLIX_PTY_ TTY_PATH_MISSING; fi
        """#,
        #"printf '%s%s\n' ORLIX_PTY_ DONE"#,
    ].joined(separator: "\r") + "\r"

    func run() throws -> String {
        guard let rootImageIdentifier =
                OrlixOSDistribution.productRootImageIdentifier
        else {
            throw OrlixPTYRuntimeProofError.missingProductRootImageMetadata
        }
        guard let profile = OrlixOSDistribution.bundledBootProfile else {
            throw OrlixPTYRuntimeProofError.missingBundledBootProfile
        }
        let session = OrlixLinuxSession(
            bootConfig: OrlixBootConfig(
                profile: profile,
                rootImageIdentifier: rootImageIdentifier,
                terminalIdentifier: "orlix.test.pty.runtime"
            )
        )
        let payloadURL = try Self.productPayloadURL()
        try Self.validateProductPayloadStamp(in: payloadURL)

        let terminalLog = PTYTerminalLog()
        terminalLog.writeLine("payload=\(payloadURL.path)")
        terminalLog.writeLine("rootImageIdentifier=\(session.bootConfig.rootImageIdentifier)")
        terminalLog.writeLine("terminalIdentifier=\(session.bootConfig.terminalIdentifier)")

        let recorder = PTYOutputRecorder(terminalLog: terminalLog)
        let bootStatus = PTYBootStatusRecorder()
        let completion = DispatchSemaphore(value: 0)
        let output = session.terminal.attachOutput { data in
            recorder.append(data)
            let text = recorder.text

            if let marker = Self.firstWrongRootfsMarker(in: text) {
                terminalLog.writeLine("wrong rootfs marker=\(marker)")
                completion.signal()
            }

            if !recorder.hasSentProofCommands,
               Self.containsShellPrompt(text) {
                terminalLog.writeLine("shell prompt detected; sending PTY proof commands")
                recorder.markProofCommandsSent()
                session.terminal.send(Data(Self.commandScript.utf8))
            }

            if Self.containsTerminalCondition(text) {
                completion.signal()
            }
        }
        defer { output.cancel() }

        DispatchQueue.global(qos: .userInitiated).async {
            terminalLog.writeLine("boot starting")
            let status = session.boot()
            terminalLog.writeLine("boot returned status=\(status.message)")
            bootStatus.set(status)
            if status != .ok {
                completion.signal()
            }
        }

        let deadline = Date().addingTimeInterval(Self.timeout)
        let firstOutputDeadline = Date().addingTimeInterval(Self.firstOutputTimeout)
        while completion.wait(timeout: .now() + .seconds(1)) != .success {
            if recorder.byteCount == 0,
               Date() >= firstOutputDeadline {
                terminalLog.writeLine("no terminal output after \(Int(Self.firstOutputTimeout)) seconds")
                throw OrlixPTYRuntimeProofError.noTerminalOutput(
                    Self.firstOutputTimeout,
                    terminalLog.url
                )
            }
            if Date() >= deadline {
                let text = recorder.text
                if let marker = Self.firstFatalMarker(in: text) {
                    throw OrlixPTYRuntimeProofError.fatalMarker(marker)
                }
                if let marker = Self.firstWrongRootfsMarker(in: text) {
                    throw OrlixPTYRuntimeProofError.wrongRootfs(marker, terminalLog.url)
                }
                terminalLog.writeLine("timeout byteCount=\(recorder.byteCount)")
                throw OrlixPTYRuntimeProofError.timeout(
                    Self.timeout,
                    text,
                    terminalLog.url,
                    terminalLog.tail()
                )
            }
        }

        if let status = bootStatus.value, status != .ok {
            throw OrlixPTYRuntimeProofError.bootFailed(status)
        }

        let text = Self.normalized(recorder.text)
        if let marker = Self.firstWrongRootfsMarker(in: text) {
            throw OrlixPTYRuntimeProofError.wrongRootfs(marker, terminalLog.url)
        }
        try Self.validate(text)
        return text
    }

    private static func productPayloadURL() throws -> URL {
        guard let payloadURL = OrlixOSPayload.bundleURL else {
            throw OrlixPTYRuntimeProofError.missingProductPayloadBundle
        }

        return payloadURL
    }

    private static func validateProductPayloadStamp(in payloadURL: URL) throws {
        let stampURL = payloadURL.appendingPathComponent(".orlix-payload-ready")
        let stamp = try String(contentsOf: stampURL, encoding: .utf8)

        guard stamp.contains("rootfs_input="),
              stamp.contains("/Build/OrlixOS/rootfs/"),
              stamp.contains("base_root_tree_input="),
              !stamp.contains("base_root_tree_input=\n"),
              stamp.contains("state_root_tree_input="),
              !stamp.contains("state_root_tree_input=\n")
        else {
            throw OrlixPTYRuntimeProofError.nonProductPayload(stamp)
        }
    }

    private static func containsTerminalCondition(_ rawOutput: String) -> Bool {
        let output = normalized(rawOutput)

        return output.contains(doneMarker) ||
            firstFatalMarker(in: output) != nil ||
            firstWrongRootfsMarker(in: output) != nil
    }

    private static func validate(_ output: String) throws {
        if let marker = firstFatalMarker(in: output) {
            throw OrlixPTYRuntimeProofError.fatalMarker(marker)
        }
        guard output.contains("ORLIX_PTY_INPUT_OK") else {
            throw OrlixPTYRuntimeProofError.missingMarker("ORLIX_PTY_INPUT_OK")
        }
        guard output.contains("ORLIX_PTY_TTY_OK") else {
            throw OrlixPTYRuntimeProofError.missingMarker("ORLIX_PTY_TTY_OK")
        }
        guard !output.contains("ORLIX_PTY_TTY_FAIL") else {
            throw OrlixPTYRuntimeProofError.missingPTYTTY
        }
        guard !output.contains("ORLIX_PTY_TTY_PATH_MISSING") else {
            throw OrlixPTYRuntimeProofError.missingTTYPathCommand
        }
        guard output.contains("/dev/pts/") else {
            throw OrlixPTYRuntimeProofError.missingPTYPath(output)
        }
        guard output.contains(doneMarker) else {
            throw OrlixPTYRuntimeProofError.missingMarker(doneMarker)
        }
    }

    private static func containsShellPrompt(_ rawOutput: String) -> Bool {
        let tail = String(normalized(rawOutput).suffix(4096))

        return tail.contains("sh-5.3# ") ||
            tail.range(
                of: #"[A-Za-z_-]*sh-[0-9][^\n#]*# "#,
                options: .regularExpression
            ) != nil
    }

    private static func normalized(_ text: String) -> String {
        stripANSI(
            text.replacingOccurrences(of: "\r\n", with: "\n")
                .replacingOccurrences(of: "\r", with: "\n")
        )
    }

    private static func stripANSI(_ text: String) -> String {
        var output = ""
        var scalars = text.unicodeScalars.makeIterator()

        while let scalar = scalars.next() {
            if scalar.value != 0x1b {
                output.unicodeScalars.append(scalar)
                continue
            }

            guard let introducer = scalars.next() else {
                break
            }
            if introducer.value != 0x5b {
                continue
            }

            while let sequenceScalar = scalars.next() {
                if sequenceScalar.value >= 0x40 &&
                    sequenceScalar.value <= 0x7e {
                    break
                }
            }
        }

        return output
    }

    private static func firstFatalMarker(in output: String) -> String? {
        [
            "Kernel panic",
            "kernel panic",
            "panic:",
            "Out of memory",
            "oom-kill",
            "Killed process",
            "orlix-init: open PTY master failed",
            "orlix-init: unlock PTY failed",
            "orlix-init: get PTY number failed",
            "orlix-init: shell TIOCSCTTY failed",
            "orlix-init: PTY shell session ended",
        ].first { output.contains($0) }
    }

    private static func firstWrongRootfsMarker(in output: String) -> String? {
        [
            "ORLIX-KSELFTEST-INIT",
            "ORLIX-MLIBC-TEST-INIT",
            "ORLIX-COREUTILS-TEST-INIT",
            "ORLIX-COREUTILS-TEST-RUNNING",
        ].first { output.contains($0) }
    }
}

private enum OrlixPTYRuntimeProofError: Error, CustomStringConvertible {
    case missingProductPayloadBundle
    case missingProductRootImageMetadata
    case missingBundledBootProfile
    case nonProductPayload(String)
    case bootFailed(OrlixBootStatus)
    case noTerminalOutput(TimeInterval, URL)
    case timeout(TimeInterval, String, URL, String)
    case wrongRootfs(String, URL)
    case fatalMarker(String)
    case missingMarker(String)
    case missingPTYTTY
    case missingTTYPathCommand
    case missingPTYPath(String)

    var description: String {
        switch self {
        case .missingProductPayloadBundle:
            return "missing OrlixOS payload bundle resolved from target metadata"
        case .missingProductRootImageMetadata:
            return "missing OrlixOS product root image identifier in target metadata"
        case .missingBundledBootProfile:
            return "missing OrlixOS payload boot profile metadata"
        case let .nonProductPayload(stamp):
            return "OrlixOS payload bundle is not packaged from the product rootfs: \(stamp)"
        case let .bootFailed(status):
            return "Orlix product boot failed: \(status.message)"
        case let .noTerminalOutput(timeout, logURL):
            return "no Linux terminal output after \(Int(timeout)) seconds; terminal log: \(logURL.path)"
        case let .timeout(timeout, output, logURL, tail):
            return "timed out after \(Int(timeout)) seconds waiting for PTY proof output; terminal log: \(logURL.path); tail: \(tail); output: \(output)"
        case let .wrongRootfs(marker, logURL):
            return "product PTY proof booted an upstream test rootfs marker \(marker); terminal log: \(logURL.path)"
        case let .fatalMarker(marker):
            return "fatal PTY runtime marker found: \(marker)"
        case let .missingMarker(marker):
            return "missing PTY runtime marker: \(marker)"
        case .missingPTYTTY:
            return "interactive shell stdin/stdout/stderr were not all ttys"
        case .missingTTYPathCommand:
            return "interactive shell did not have a tty command"
        case let .missingPTYPath(output):
            return "interactive shell tty output did not contain /dev/pts/: \(output)"
        }
    }
}

private final class PTYTerminalLog: @unchecked Sendable {
    let url: URL

    private let lock = NSLock()

    init() {
        let fileName = "orlix-pty-runtime-\(UUID().uuidString).log"
        url = FileManager.default.temporaryDirectory.appendingPathComponent(fileName)
        FileManager.default.createFile(atPath: url.path, contents: nil)
    }

    func writeLine(_ line: String) {
        append(Data("[orlix-pty] \(line)\n".utf8))
    }

    func append(_ data: Data) {
        lock.lock()
        defer { lock.unlock() }

        guard let handle = FileHandle(forWritingAtPath: url.path) else {
            return
        }
        handle.seekToEndOfFile()
        handle.write(data)
        handle.closeFile()
    }

    func tail(limit: Int = 4096) -> String {
        lock.lock()
        defer { lock.unlock() }

        guard let data = try? Data(contentsOf: url) else {
            return ""
        }
        return String(decoding: data.suffix(limit), as: UTF8.self)
    }
}

private final class PTYBootStatusRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var storage: OrlixBootStatus?

    var value: OrlixBootStatus? {
        lock.lock()
        defer { lock.unlock() }
        return storage
    }

    func set(_ status: OrlixBootStatus) {
        lock.lock()
        storage = status
        lock.unlock()
    }
}

private final class PTYOutputRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private let terminalLog: PTYTerminalLog
    private var storage = Data()
    private var sentProofCommands = false

    init(terminalLog: PTYTerminalLog) {
        self.terminalLog = terminalLog
    }

    var text: String {
        lock.lock()
        defer { lock.unlock() }
        return String(decoding: storage, as: UTF8.self)
    }

    var hasSentProofCommands: Bool {
        lock.lock()
        defer { lock.unlock() }
        return sentProofCommands
    }

    var byteCount: Int {
        lock.lock()
        defer { lock.unlock() }
        return storage.count
    }

    func append(_ data: Data) {
        lock.lock()
        storage.append(data)
        lock.unlock()
        terminalLog.append(data)
    }

    func markProofCommandsSent() {
        lock.lock()
        sentProofCommands = true
        lock.unlock()
    }
}
