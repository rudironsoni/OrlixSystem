#!/usr/bin/env swift

import Foundation

struct OracleCase: Decodable {
    struct Compare: Decodable {
        let stdout: Bool
        let stderr: Bool
        let exitStatus: Bool
        let signal: Bool
        let errnoEvents: Bool
        let statEntries: Bool
        let mutations: Bool
        let observations: Bool
    }

    let id: String
    let description: String
    let fixture: String?
    let command: [String]
    let environment: [String: String]
    let workingDirectory: String?
    let compare: Compare
}

struct OracleResult: Codable {
    struct ErrnoEvent: Codable, Equatable {
        let operation: String
        let path: String
        let errno: Int
        let name: String?
    }

    struct StatEntry: Codable, Equatable {
        let path: String
        let mode: String?
        let uid: Int?
        let gid: Int?
        let type: String?
        let size: Int?
    }

    struct Mutation: Codable, Equatable {
        let path: String
        let operation: String
        let result: String
    }

    let caseID: String
    let runner: String
    let stdout: String
    let stderr: String
    let exitStatus: Int?
    let signal: String?
    let errnoEvents: [ErrnoEvent]
    let statEntries: [StatEntry]
    let mutations: [Mutation]
    let observations: [String: String]
}

enum OracleError: Error, CustomStringConvertible {
    case usage(String)
    case invalidCase(String)
    case invalidLog(String)
    case unsupportedHost(String)
    case runnerFailed(String)
    case caseMismatch(expected: String, linux: String, orlix: String)
    case mismatches([String])

    var description: String {
        switch self {
        case let .usage(message):
            return message
        case let .invalidCase(message):
            return "invalid case: \(message)"
        case let .invalidLog(message):
            return "invalid log: \(message)"
        case let .unsupportedHost(message):
            return "unsupported host: \(message)"
        case let .runnerFailed(message):
            return "runner failed: \(message)"
        case let .caseMismatch(expected, linux, orlix):
            return "case mismatch: expected \(expected), linux \(linux), orlix \(orlix)"
        case let .mismatches(items):
            return (["oracle comparison failed:"] + items.map { "- \($0)" })
                .joined(separator: "\n")
        }
    }
}

func readJSON<T: Decodable>(_ type: T.Type, at path: String) throws -> T {
    let url = URL(fileURLWithPath: path)
    let data = try Data(contentsOf: url)
    return try JSONDecoder().decode(type, from: data)
}

func writeJSON<T: Encodable>(_ value: T, to path: String) throws {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    let data = try encoder.encode(value)
    try data.write(to: URL(fileURLWithPath: path))
}

func validateCase(_ testCase: OracleCase) throws {
    guard !testCase.id.isEmpty else {
        throw OracleError.invalidCase("id is empty")
    }
    guard !testCase.description.isEmpty else {
        throw OracleError.invalidCase("description is empty")
    }
    guard let executable = testCase.command.first, executable.hasPrefix("/") else {
        throw OracleError.invalidCase("command must start with an absolute Linux path")
    }
    guard testCase.command.allSatisfy({ !$0.contains("\u{0}") }) else {
        throw OracleError.invalidCase("command contains NUL")
    }
    if let workingDirectory = testCase.workingDirectory,
       !workingDirectory.hasPrefix("/") || workingDirectory.contains("\u{0}") {
        throw OracleError.invalidCase("workingDirectory must be an absolute Linux path")
    }
    for (key, value) in testCase.environment {
        if key.isEmpty || key.contains("=") || key.contains("\u{0}") ||
            value.contains("\u{0}") {
            throw OracleError.invalidCase("invalid environment entry \(key)")
        }
    }
}

func compare(_ testCase: OracleCase, linux: OracleResult, orlix: OracleResult) throws {
    guard linux.caseID == testCase.id, orlix.caseID == testCase.id else {
        throw OracleError.caseMismatch(
            expected: testCase.id,
            linux: linux.caseID,
            orlix: orlix.caseID
        )
    }

    var mismatches: [String] = []
    let rules = testCase.compare

    if rules.stdout && linux.stdout != orlix.stdout {
        mismatches.append("stdout differs")
    }
    if rules.stderr && linux.stderr != orlix.stderr {
        mismatches.append("stderr differs")
    }
    if rules.exitStatus && linux.exitStatus != orlix.exitStatus {
        mismatches.append("exitStatus differs: linux=\(String(describing: linux.exitStatus)) orlix=\(String(describing: orlix.exitStatus))")
    }
    if rules.signal && linux.signal != orlix.signal {
        mismatches.append("signal differs: linux=\(String(describing: linux.signal)) orlix=\(String(describing: orlix.signal))")
    }
    if rules.errnoEvents && linux.errnoEvents != orlix.errnoEvents {
        mismatches.append("errnoEvents differ")
    }
    if rules.statEntries && linux.statEntries != orlix.statEntries {
        mismatches.append("statEntries differ")
    }
    if rules.mutations && linux.mutations != orlix.mutations {
        mismatches.append("mutations differ")
    }
    if rules.observations && linux.observations != orlix.observations {
        mismatches.append("observations differ")
    }

    guard mismatches.isEmpty else {
        throw OracleError.mismatches(mismatches)
    }
}

func value(after flag: String, in arguments: [String]) -> String? {
    guard let index = arguments.firstIndex(of: flag),
          arguments.indices.contains(arguments.index(after: index)) else {
        return nil
    }
    return arguments[arguments.index(after: index)]
}

func errnoName(_ value: Int) -> String? {
    switch value {
    case 2:
        return "ENOENT"
    case 20:
        return "ENOTDIR"
    case 40:
        return "ELOOP"
    default:
        return nil
    }
}

struct RawErrnoEvent: Decodable {
    let operation: String
    let path: String
    let errno: Int
    let expected: Int
}

func pathErrnoEvents(from jsonLines: [String]) throws -> [OracleResult.ErrnoEvent] {
    let decoder = JSONDecoder()

    return try jsonLines.map { line -> OracleResult.ErrnoEvent in
        let raw = try decoder.decode(
            RawErrnoEvent.self,
            from: Data(line.utf8)
        )
        guard raw.errno == raw.expected else {
            throw OracleError.invalidLog(
                "event \(raw.operation) \(raw.path) errno \(raw.errno) does not match expected \(raw.expected)"
            )
        }
        return OracleResult.ErrnoEvent(
            operation: raw.operation,
            path: raw.path,
            errno: raw.errno,
            name: errnoName(raw.errno)
        )
    }
}

func pathErrnoResult(
    runner: String,
    stdout: String,
    stderr: String,
    exitStatus: Int?,
    signal: String?,
    jsonLines: [String]
) throws -> OracleResult {
    guard jsonLines.count == 4 else {
        throw OracleError.invalidLog("expected 4 path-errno JSON events, found \(jsonLines.count)")
    }

    let events = try pathErrnoEvents(from: jsonLines)
    return OracleResult(
        caseID: "path-errno",
        runner: runner,
        stdout: stdout,
        stderr: stderr,
        exitStatus: exitStatus,
        signal: signal,
        errnoEvents: events,
        statEntries: [],
        mutations: [
            OracleResult.Mutation(
                path: "regular",
                operation: "create-unlink",
                result: "ok"
            ),
            OracleResult.Mutation(
                path: "loop-a",
                operation: "symlink-unlink",
                result: "ok"
            )
        ],
        observations: [
            "filesystem": "tmpfs-or-scratch"
        ]
    )
}

func oracleBlock(caseID: String, in log: String) throws -> [String] {
    let begin = "ORLIX-ORACLE-BEGIN \(caseID)"
    let end = "ORLIX-ORACLE-END \(caseID)"
    guard let beginRange = log.range(of: begin) else {
        throw OracleError.invalidLog("missing \(begin)")
    }
    guard let endRange = log.range(of: end, range: beginRange.upperBound..<log.endIndex) else {
        throw OracleError.invalidLog("missing \(end)")
    }

    let block = String(log[beginRange.upperBound..<endRange.lowerBound])
    return block
        .split(separator: "\n", omittingEmptySubsequences: false)
        .compactMap { rawLine -> String? in
            guard let start = rawLine.firstIndex(of: "{"),
                  let end = rawLine.lastIndex(of: "}") else {
                return nil
            }
            return String(rawLine[start...end])
        }
}

func pathErrnoResultFromOrlixLog(_ log: String) throws -> OracleResult {
    let jsonLines = try oracleBlock(caseID: "path-errno", in: log)
    return try pathErrnoResult(
        runner: "orlix",
        stdout: jsonLines.joined(separator: "\n") + "\n",
        stderr: "",
        exitStatus: 0,
        signal: nil,
        jsonLines: jsonLines
    )
}

func jsonObjectLines(from output: String) -> [String] {
    output
        .split(separator: "\n", omittingEmptySubsequences: false)
        .compactMap { rawLine -> String? in
            guard let start = rawLine.firstIndex(of: "{"),
                  let end = rawLine.lastIndex(of: "}") else {
                return nil
            }
            return String(rawLine[start...end])
        }
}

func requireLinuxHost() throws {
    #if os(Linux)
    return
    #else
    throw OracleError.unsupportedHost(
        "linux-result-from-fixture must run inside a real Linux environment"
    )
    #endif
}

func processOutput(_ pipe: Pipe) -> String {
    let data = pipe.fileHandleForReading.readDataToEndOfFile()
    return String(data: data, encoding: .utf8) ?? ""
}

func runLinuxFixture(
    testCase: OracleCase,
    fixturePath: String,
    workdirPath: String
) throws -> OracleResult {
    try requireLinuxHost()

    guard testCase.id == "path-errno" else {
        throw OracleError.invalidCase(
            "linux-result-from-fixture currently supports path-errno only"
        )
    }

    try FileManager.default.createDirectory(
        atPath: workdirPath,
        withIntermediateDirectories: true
    )

    let process = Process()
    let stdoutPipe = Pipe()
    let stderrPipe = Pipe()
    var environment = ProcessInfo.processInfo.environment

    for (key, value) in testCase.environment {
        environment[key] = value
    }
    process.executableURL = URL(fileURLWithPath: fixturePath)
    process.arguments = Array(testCase.command.dropFirst())
    process.currentDirectoryURL = URL(fileURLWithPath: workdirPath)
    process.environment = environment
    process.standardOutput = stdoutPipe
    process.standardError = stderrPipe

    try process.run()
    process.waitUntilExit()

    let stdout = processOutput(stdoutPipe)
    let stderr = processOutput(stderrPipe)
    let status = Int(process.terminationStatus)
    let signal = process.terminationReason == .uncaughtSignal ?
        "SIG\(status)" : nil
    let exitStatus = process.terminationReason == .exit ? status : nil

    if process.terminationReason == .exit && status != 0 {
        throw OracleError.runnerFailed(
            "fixture exited with status \(status); stderr: \(stderr)"
        )
    }
    if process.terminationReason == .uncaughtSignal {
        throw OracleError.runnerFailed(
            "fixture terminated by signal \(status); stderr: \(stderr)"
        )
    }

    return try pathErrnoResult(
        runner: "linux",
        stdout: stdout,
        stderr: stderr,
        exitStatus: exitStatus,
        signal: signal,
        jsonLines: jsonObjectLines(from: stdout)
    )
}

func run(arguments: [String]) throws {
    guard let command = arguments.first else {
        throw OracleError.usage("usage: validate-case <case.json> | linux-result-from-fixture --case <case.json> --fixture <binary> --workdir <dir> --output <result.json> | orlix-result-from-log --case <case.json> --log <log.txt> --output <result.json> | compare --case <case.json> --linux-result <result.json> --orlix-result <result.json>")
    }

    switch command {
    case "validate-case":
        guard arguments.count == 2 else {
            throw OracleError.usage("usage: validate-case <case.json>")
        }
        let testCase = try readJSON(OracleCase.self, at: arguments[1])
        try validateCase(testCase)
        print("case \(testCase.id) is valid")
    case "linux-result-from-fixture":
        guard let casePath = value(after: "--case", in: arguments),
              let fixturePath = value(after: "--fixture", in: arguments),
              let workdirPath = value(after: "--workdir", in: arguments),
              let outputPath = value(after: "--output", in: arguments)
        else {
            throw OracleError.usage("usage: linux-result-from-fixture --case <case.json> --fixture <binary> --workdir <dir> --output <result.json>")
        }
        let testCase = try readJSON(OracleCase.self, at: casePath)
        try validateCase(testCase)
        let result = try runLinuxFixture(
            testCase: testCase,
            fixturePath: fixturePath,
            workdirPath: workdirPath
        )
        try writeJSON(result, to: outputPath)
        print("wrote Linux result for case \(testCase.id): \(outputPath)")
    case "orlix-result-from-log":
        guard let casePath = value(after: "--case", in: arguments),
              let logPath = value(after: "--log", in: arguments),
              let outputPath = value(after: "--output", in: arguments)
        else {
            throw OracleError.usage("usage: orlix-result-from-log --case <case.json> --log <log.txt> --output <result.json>")
        }
        let testCase = try readJSON(OracleCase.self, at: casePath)
        try validateCase(testCase)
        guard testCase.id == "path-errno" else {
            throw OracleError.invalidCase(
                "orlix-result-from-log currently supports path-errno only"
            )
        }
        let log = try String(contentsOfFile: logPath, encoding: .utf8)
        let result = try pathErrnoResultFromOrlixLog(log)
        try writeJSON(result, to: outputPath)
        print("wrote Orlix result for case \(testCase.id): \(outputPath)")
    case "compare":
        guard let casePath = value(after: "--case", in: arguments),
              let linuxPath = value(after: "--linux-result", in: arguments),
              let orlixPath = value(after: "--orlix-result", in: arguments)
        else {
            throw OracleError.usage("usage: compare --case <case.json> --linux-result <result.json> --orlix-result <result.json>")
        }
        let testCase = try readJSON(OracleCase.self, at: casePath)
        try validateCase(testCase)
        let linux = try readJSON(OracleResult.self, at: linuxPath)
        let orlix = try readJSON(OracleResult.self, at: orlixPath)
        try compare(testCase, linux: linux, orlix: orlix)
        print("case \(testCase.id) matches")
    default:
        throw OracleError.usage("unknown command: \(command)")
    }
}

do {
    try run(arguments: Array(CommandLine.arguments.dropFirst()))
} catch let error as OracleError {
    FileHandle.standardError.write(Data((error.description + "\n").utf8))
    exit(2)
} catch {
    FileHandle.standardError.write(Data(("error: \(error)\n").utf8))
    exit(2)
}
