import Foundation
import OrlixKit

enum OrlixUpstreamTestSuite: String, Sendable {
    case kernel
    case mlibc
    case coreutils
}

struct OrlixUpstreamTestRunSpec: Equatable, Sendable {
    let suite: OrlixUpstreamTestSuite
    let rootImageIdentifier: String
    let rootBundleResourceName: String
    let completionMarker: String
    let expectedCoreutilsTotal: Int?
    let timeout: TimeInterval

    var bootConfig: OrlixBootConfig {
        OrlixBootConfig(
            profile: .development,
            rootImageIdentifier: rootImageIdentifier,
            terminalIdentifier: "orlix.test.\(suite.rawValue).terminal"
        )
    }

    static let kernel = OrlixUpstreamTestRunSpec(
        suite: .kernel,
        rootImageIdentifier: "orlix.test.kselftest.rootfs",
        rootBundleResourceName: "OrlixTestInitramfs",
        completionMarker: "ORLIX-KSELFTEST-END",
        expectedCoreutilsTotal: nil,
        timeout: 300
    )

    static let mlibc = OrlixUpstreamTestRunSpec(
        suite: .mlibc,
        rootImageIdentifier: "orlix.test.mlibc.rootfs",
        rootBundleResourceName: "OrlixMLibCTestInitramfs",
        completionMarker: "ORLIX-MLIBC-TEST-END",
        expectedCoreutilsTotal: nil,
        timeout: 1_200
    )

    static let coreutils = OrlixUpstreamTestRunSpec(
        suite: .coreutils,
        rootImageIdentifier: "orlix.test.coreutils.rootfs",
        rootBundleResourceName: "CoreutilsTestInitramfs",
        completionMarker: "ORLIX-COREUTILS-TEST-END",
        expectedCoreutilsTotal: 733,
        timeout: 3_600
    )
}

enum OrlixUpstreamTestRunError: Error, Equatable, CustomStringConvertible {
    case missingRootfsBundle(String)
    case bootFailed(OrlixBootStatus)
    case timeout(TimeInterval)
    case crashReport(String)
    case kernelPanic(String)
    case oom(String)
    case upstreamFailure(String)
    case missingCompletionMarker(String)
    case malformedUpstreamOutput(String)
    case malformedCoreutilsCompletion(String)
    case coreutilsSummaryFailed(failures: Int, skips: Int, total: Int, expectedTotal: Int)

    var description: String {
        switch self {
        case let .missingRootfsBundle(bundle):
            return "missing upstream test rootfs bundle: \(bundle).bundle"
        case let .bootFailed(status):
            return "Orlix boot failed: \(status.message)"
        case let .timeout(timeout):
            return "timed out after \(Int(timeout)) seconds waiting for upstream test output"
        case let .crashReport(marker):
            return "host crash report marker found: \(marker)"
        case let .kernelPanic(marker):
            return "kernel panic marker found: \(marker)"
        case let .oom(marker):
            return "out-of-memory marker found: \(marker)"
        case let .upstreamFailure(line):
            return "upstream failure marker found: \(line)"
        case let .missingCompletionMarker(marker):
            return "missing upstream completion marker: \(marker)"
        case let .malformedUpstreamOutput(reason):
            return "malformed upstream output: \(reason)"
        case let .malformedCoreutilsCompletion(line):
            return "malformed Coreutils completion marker: \(line)"
        case let .coreutilsSummaryFailed(failures, skips, total, expectedTotal):
            return "Coreutils summary failed: failures=\(failures) skips=\(skips) total=\(total) expectedTotal=\(expectedTotal)"
        }
    }
}

final class OrlixUpstreamTestOutputParser {
    func validate(
        _ rawOutput: String,
        for spec: OrlixUpstreamTestRunSpec
    ) throws {
        let output = Self.normalized(rawOutput)

        if let marker = Self.firstMarker(
            in: output,
            markers: ["Incident Identifier:", "Exception Type:", "Termination Reason:"]
        ) {
            throw OrlixUpstreamTestRunError.crashReport(marker)
        }
        if let marker = Self.firstMarker(
            in: output,
            markers: ["Kernel panic", "kernel panic", "panic:"]
        ) {
            throw OrlixUpstreamTestRunError.kernelPanic(marker)
        }
        if let marker = Self.firstMarker(
            in: output,
            markers: ["Out of memory", "oom-kill", "Killed process"]
        ) {
            throw OrlixUpstreamTestRunError.oom(marker)
        }
        if let failure = Self.firstUpstreamFailureLine(in: output) {
            throw OrlixUpstreamTestRunError.upstreamFailure(failure)
        }
        guard output.contains(spec.completionMarker) else {
            throw OrlixUpstreamTestRunError.missingCompletionMarker(
                spec.completionMarker
            )
        }

        switch spec.suite {
        case .kernel, .mlibc:
            guard output.contains("TAP version 13") else {
                throw OrlixUpstreamTestRunError.malformedUpstreamOutput(
                    "missing TAP version 13"
                )
            }
            guard Self.containsTAPPlan(in: output) else {
                throw OrlixUpstreamTestRunError.malformedUpstreamOutput(
                    "missing TAP plan"
                )
            }
        case .coreutils:
            try validateCoreutilsCompletion(output, for: spec)
        }
    }

    private func validateCoreutilsCompletion(
        _ output: String,
        for spec: OrlixUpstreamTestRunSpec
    ) throws {
        guard let line = output
            .split(separator: "\n", omittingEmptySubsequences: false)
            .last(where: { $0.contains(spec.completionMarker) })
            .map(String.init)
        else {
            throw OrlixUpstreamTestRunError.missingCompletionMarker(
                spec.completionMarker
            )
        }

        let fields = line.split(separator: " ")
        guard
            fields.count == 4,
            fields[0] == Substring(spec.completionMarker),
            let failures = Self.value(after: "failures=", in: fields[1]),
            let skips = Self.value(after: "skips=", in: fields[2]),
            let total = Self.value(after: "total=", in: fields[3]),
            let expectedTotal = spec.expectedCoreutilsTotal
        else {
            throw OrlixUpstreamTestRunError.malformedCoreutilsCompletion(line)
        }

        guard failures == 0, skips == 0, total == expectedTotal else {
            throw OrlixUpstreamTestRunError.coreutilsSummaryFailed(
                failures: failures,
                skips: skips,
                total: total,
                expectedTotal: expectedTotal
            )
        }
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
                if sequenceScalar.value >= 0x40 && sequenceScalar.value <= 0x7e {
                    break
                }
            }
        }

        return output
    }

    private static func firstMarker(
        in output: String,
        markers: [String]
    ) -> String? {
        markers.first { output.contains($0) }
    }

    private static func firstUpstreamFailureLine(in output: String) -> String? {
        output
            .split(separator: "\n", omittingEmptySubsequences: false)
            .map(String.init)
            .first { line in
                let trimmed = line.trimmingCharacters(in: .whitespaces)
                return trimmed.hasPrefix("not ok")
            }
    }

    private static func containsTAPPlan(in output: String) -> Bool {
        output
            .split(separator: "\n", omittingEmptySubsequences: false)
            .contains { line in
                line.hasPrefix("1..") &&
                    line.dropFirst(3).allSatisfy(\.isNumber)
            }
    }

    private static func value(
        after prefix: String,
        in field: Substring
    ) -> Int? {
        guard field.hasPrefix(prefix) else {
            return nil
        }
        return Int(field.dropFirst(prefix.count))
    }
}

final class OrlixUpstreamTestSessionRunner: @unchecked Sendable {
    private let spec: OrlixUpstreamTestRunSpec
    private let session: OrlixLinuxSession
    private let parser: OrlixUpstreamTestOutputParser

    init(
        spec: OrlixUpstreamTestRunSpec,
        session: OrlixLinuxSession? = nil,
        parser: OrlixUpstreamTestOutputParser = OrlixUpstreamTestOutputParser()
    ) {
        self.spec = spec
        self.session = session ?? OrlixLinuxSession(bootConfig: spec.bootConfig)
        self.parser = parser
    }

    func run() throws -> String {
        guard Bundle.main.url(
            forResource: spec.rootBundleResourceName,
            withExtension: "bundle"
        ) != nil else {
            throw OrlixUpstreamTestRunError.missingRootfsBundle(
                spec.rootBundleResourceName
            )
        }

        let recorder = TerminalOutputRecorder()
        let completion = DispatchSemaphore(value: 0)
        let output = session.terminal.attachOutput { data in
            recorder.append(data)
            if recorder.text.contains(self.spec.completionMarker) {
                completion.signal()
            }
        }
        defer { output.cancel() }

        let status = session.boot()
        guard status == .ok else {
            throw OrlixUpstreamTestRunError.bootFailed(status)
        }

        let deadline = DispatchTime.now() + spec.timeout
        guard completion.wait(timeout: deadline) == .success else {
            let text = recorder.text
            do {
                try parser.validate(text, for: spec)
            } catch let error as OrlixUpstreamTestRunError {
                throw error
            }
            throw OrlixUpstreamTestRunError.timeout(spec.timeout)
        }

        let text = recorder.text
        try parser.validate(text, for: spec)
        return text
    }
}

private final class TerminalOutputRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var storage = Data()

    var text: String {
        lock.lock()
        defer { lock.unlock() }
        return String(decoding: storage, as: UTF8.self)
    }

    func append(_ data: Data) {
        lock.lock()
        storage.append(data)
        lock.unlock()
    }
}
