import Foundation
@_spi(OrlixPrivateTesting) import OrlixOS

enum OrlixUpstreamTestSuite: String, Sendable {
    case kernel
    case mlibc
    case coreutils
}

struct OrlixUpstreamTestRunSpec: Equatable, Sendable {
    let suite: OrlixUpstreamTestSuite
    let completionMarker: String
    let expectedCoreutilsTotal: Int?
    let timeout: TimeInterval

    func bootConfig(rootImage: OrlixRootImageDescriptor) throws
        -> OrlixBootConfig
    {
        guard let profile = OrlixOSDistribution.bundledBootProfile else {
            throw OrlixUpstreamTestRunError.missingBundledBootProfile
        }

        return OrlixBootConfig(
            profile: profile,
            kernelCommandLine: rootImage.kernelCommandLine,
            rootImageIdentifier: rootImage.identifier,
            terminalIdentifier: "orlix.test.\(suite.rawValue).terminal"
        )
    }

    func rootImageDescriptor() throws -> OrlixRootImageDescriptor {
        guard let descriptor = OrlixOSDistribution.rootImageDescriptor(
            forRole: suite.rawValue
        ) else {
            throw OrlixUpstreamTestRunError.missingRootImageDescriptor(
                suite.rawValue
            )
        }

        return descriptor
    }

    static let kernel = OrlixUpstreamTestRunSpec(
        suite: .kernel,
        completionMarker: "ORLIX-KSELFTEST-END",
        expectedCoreutilsTotal: nil,
        timeout: 300
    )

    static let mlibc = OrlixUpstreamTestRunSpec(
        suite: .mlibc,
        completionMarker: "ORLIX-MLIBC-TEST-END",
        expectedCoreutilsTotal: nil,
        timeout: 1_200
    )

    static let coreutils = OrlixUpstreamTestRunSpec(
        suite: .coreutils,
        completionMarker: "ORLIX-COREUTILS-TEST-END",
        expectedCoreutilsTotal: 733,
        timeout: 14_400
    )
}

enum OrlixUpstreamTestRunError: Error, Equatable, CustomStringConvertible {
    case missingRootImageDescriptor(String)
    case missingRootfsBundle(String)
    case missingBundledBootProfile
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
        case let .missingRootImageDescriptor(role):
            return "missing OrlixOS root image metadata for role: \(role)"
        case let .missingRootfsBundle(bundle):
            return "missing upstream test rootfs bundle: \(bundle)"
        case .missingBundledBootProfile:
            return "missing OrlixOS payload boot profile metadata"
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
    func containsTerminalCondition(
        _ rawOutput: String,
        for spec: OrlixUpstreamTestRunSpec
    ) -> Bool {
        let output = Self.normalized(rawOutput)

        return output.contains(spec.completionMarker) ||
            Self.firstFatalMarker(in: output) != nil ||
            Self.firstUpstreamFailureLine(in: output) != nil
    }

    func validate(
        _ rawOutput: String,
        for spec: OrlixUpstreamTestRunSpec
    ) throws {
        let output = Self.normalized(rawOutput)

        if let marker = Self.firstMarker(in: output, markers: Self.crashMarkers) {
            throw OrlixUpstreamTestRunError.crashReport(marker)
        }
        if let marker = Self.firstMarker(in: output, markers: Self.panicMarkers) {
            throw OrlixUpstreamTestRunError.kernelPanic(marker)
        }
        if let marker = Self.firstMarker(in: output, markers: Self.oomMarkers) {
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

    private static func firstFatalMarker(in output: String) -> String? {
        firstMarker(in: output, markers: crashMarkers) ??
            firstMarker(in: output, markers: panicMarkers) ??
            firstMarker(in: output, markers: oomMarkers)
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

    private static let crashMarkers = [
        "Incident Identifier:",
        "Exception Type:",
        "Termination Reason:",
    ]
    private static let panicMarkers = [
        "Kernel panic",
        "kernel panic",
        "panic:",
    ]
    private static let oomMarkers = [
        "Out of memory",
        "oom-kill",
        "Killed process",
    ]
}

final class OrlixUpstreamTestSessionRunner: @unchecked Sendable {
    private let spec: OrlixUpstreamTestRunSpec
    private let injectedSession: OrlixLinuxSession?
    private let parser: OrlixUpstreamTestOutputParser

    init(
        spec: OrlixUpstreamTestRunSpec,
        session: OrlixLinuxSession? = nil,
        parser: OrlixUpstreamTestOutputParser = OrlixUpstreamTestOutputParser()
    ) {
        self.spec = spec
        self.injectedSession = session
        self.parser = parser
    }

    func run() throws -> String {
        let rootImage = try spec.rootImageDescriptor()
        guard let rootBundleResourceName = rootImage.initrdBundleName else {
            throw OrlixUpstreamTestRunError.missingRootfsBundle(
                "metadata:\(spec.suite.rawValue)"
            )
        }
        guard let rootBundleExtension = rootImage.initrdBundleExtension else {
            throw OrlixUpstreamTestRunError.missingRootfsBundle(
                "metadata:\(rootBundleResourceName)"
            )
        }
        guard Bundle.main.url(
            forResource: rootBundleResourceName,
            withExtension: rootBundleExtension
        ) != nil else {
            throw OrlixUpstreamTestRunError.missingRootfsBundle(
                "\(rootBundleResourceName).\(rootBundleExtension)"
            )
        }

        let session: OrlixLinuxSession
        if let injectedSession {
            session = injectedSession
        } else {
            session = OrlixLinuxSession(
                bootConfig: try spec.bootConfig(
                    rootImage: rootImage
                )
            )
        }
        let recorder = TerminalOutputRecorder()
        let completion = DispatchSemaphore(value: 0)
        let bootStatus = BootStatusRecorder()
        let output = session.terminal.attachOutput { data in
            recorder.append(data)
            if self.parser.containsTerminalCondition(recorder.text, for: self.spec) {
                completion.signal()
            }
        }
        defer { output.cancel() }

        DispatchQueue.global(qos: .userInitiated).async {
            let status = session.boot()
            bootStatus.set(status)
            if status != .ok {
                completion.signal()
            }
        }

        let deadline = DispatchTime.now() + spec.timeout
        guard completion.wait(timeout: deadline) == .success else {
            let text = recorder.text
            if parser.containsTerminalCondition(text, for: spec) {
                try parser.validate(text, for: spec)
                return text
            }
            throw OrlixUpstreamTestRunError.timeout(spec.timeout)
        }

        if let status = bootStatus.value, status != .ok {
            throw OrlixUpstreamTestRunError.bootFailed(status)
        }

        let text = recorder.text
        try parser.validate(text, for: spec)
        return text
    }
}

private final class BootStatusRecorder: @unchecked Sendable {
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
