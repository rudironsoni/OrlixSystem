import Foundation

final class TerminalProofDriver {
    private struct Step {
        let name: String
        let command: String
        let expectedOutput: String
    }

    private static let prompt = "sh-5.3#"

    private let sendInput: (Data) -> Void
    private let log: (String) -> Void
    private let steps: [Step]
    private var started = false
    private var failed = false
    private var stepIndex = 0
    private var preStartOutput = ""
    private var stepOutput = ""

    static func fromProcessArguments(
        sendInput: @escaping (Data) -> Void
    ) -> TerminalProofDriver? {
        fromArguments(ProcessInfo.processInfo.arguments, sendInput: sendInput)
    }

    static func fromArguments(
        _ arguments: [String],
        sendInput: @escaping (Data) -> Void
    ) -> TerminalProofDriver? {
        let enabled = arguments.contains {
            $0 == "--orlix-terminal-proof=shell-package"
        }
        guard enabled else {
            return nil
        }

        return TerminalProofDriver(sendInput: sendInput)
    }

    init(
        sendInput: @escaping (Data) -> Void,
        log: @escaping (String) -> Void = TerminalProofDriver.standardErrorLog
    ) {
        self.sendInput = sendInput
        self.log = log
        self.steps = [
            Step(
                name: "echo",
                command: #"printf '\117\122\114\111\130\137\105\103\110\117\012'"#,
                expectedOutput: "ORLIX_ECHO"
            ),
            Step(
                name: "pwd",
                command: #"test "$(pwd)" = / && printf '\120\127\104\137\117\113\012'"#,
                expectedOutput: "PWD_OK"
            ),
            Step(
                name: "cd",
                command: #"cd /tmp; test "$(pwd)" = /tmp && printf '\103\104\137\117\113\012'"#,
                expectedOutput: "CD_OK"
            ),
            Step(
                name: "redirection",
                command: #"printf '\122\105\104\111\122\137\117\113\012' > /tmp/orlix-proof.txt; cat /tmp/orlix-proof.txt"#,
                expectedOutput: "REDIR_OK"
            ),
            Step(
                name: "pipe",
                command: #"printf '\120\111\120\105\137\117\113\012' | grep PIPE"#,
                expectedOutput: "PIPE_OK"
            ),
            Step(
                name: "fork-exec-wait-status",
                command: #"sh -c 'exit 7'; printf 'status:%s\n' "$?""#,
                expectedOutput: "status:7"
            ),
            Step(
                name: "signal-trap",
                command: #"/bin/bash -c 'trap "printf \"\123\111\107\137\117\113\012\"; exit 0" TERM; kill -TERM $$; :'"#,
                expectedOutput: "SIG_OK"
            ),
            Step(
                name: "jq",
                command: #"/usr/bin/jq -n '{orlix:true}'"#,
                expectedOutput: #""orlix": true"#
            ),
            Step(
                name: "curl",
                command: "/usr/bin/curl --version",
                expectedOutput: "Release-Date:"
            ),
            Step(
                name: "zsh",
                command: #"/usr/bin/zsh -fc "printf '\132\123\110\137\117\113\012'""#,
                expectedOutput: "ZSH_OK"
            ),
        ]
    }

    func receive(_ text: String) {
        guard !failed, stepIndex < steps.count else {
            return
        }

        if !started {
            preStartOutput += text
            if preStartOutput.count > 4096 {
                preStartOutput.removeFirst(preStartOutput.count - 4096)
            }
            guard preStartOutput.contains(Self.prompt) else {
                return
            }
            started = true
            preStartOutput.removeAll(keepingCapacity: false)
            log("ORLIX-TERMINAL-PROOF-BEGIN shell-package")
            sendCurrentStep()
            return
        }

        stepOutput += text
        let current = steps[stepIndex]
        let matchingOutput = Self.textForMatching(stepOutput)
        if matchingOutput.contains(current.expectedOutput),
           matchingOutput.contains(Self.prompt) {
            log("ORLIX-TERMINAL-PROOF-OK \(current.name)")
            stepIndex += 1
            stepOutput.removeAll(keepingCapacity: true)
            if stepIndex == steps.count {
                log("ORLIX-TERMINAL-PROOF-END shell-package status=0")
            } else {
                sendCurrentStep()
            }
        }
    }

    private func sendCurrentStep() {
        guard stepIndex < steps.count else {
            return
        }
        let current = steps[stepIndex]
        guard let data = "\(current.command)\r".data(using: .utf8) else {
            failed = true
            log("ORLIX-TERMINAL-PROOF-FAIL \(current.name) reason=encoding")
            return
        }

        sendInput(data)
    }

    private static func standardErrorLog(_ line: String) {
        let text = "\(line)\n"
        if let data = text.data(using: .utf8) {
            FileHandle.standardError.write(data)
        }
    }

    private static func textForMatching(_ text: String) -> String {
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
}
