import Foundation

@_silgen_name("OrlixBoot")
private func OrlixBoot(_ config: UnsafePointer<COrlixBootConfig>) -> CInt

@_silgen_name("orlix_host_console_set_output_fd")
private func orlix_host_console_set_output_fd(_ fd: CInt)

@_silgen_name("orlix_host_console_enqueue_input")
private func orlix_host_console_enqueue_input(
    _ bytes: UnsafeRawPointer?,
    _ length: UInt
) -> UInt

private struct COrlixBootConfig {
    var profile: CInt
    var rootImageIdentifier: UnsafePointer<CChar>?
    var terminalIdentifier: UnsafePointer<CChar>?
}

public enum OrlixBootProfile: Sendable {
    case release
    case development

    fileprivate var cValue: CInt {
        switch self {
        case .release:
            return 0
        case .development:
            return 1
        }
    }
}

public enum OrlixBootStatus: Equatable, Sendable {
    case ok
    case invalidConfig
    case unavailable
    case unknown(CInt)

    fileprivate init(rawStatus: CInt) {
        switch rawStatus {
        case 0:
            self = .ok
        case -1:
            self = .invalidConfig
        case -2:
            self = .unavailable
        default:
            self = .unknown(rawStatus)
        }
    }

    public var message: String {
        switch self {
        case .ok:
            return "Orlix boot entered the Linux kernel path."
        case .invalidConfig:
            return "Orlix bootloader rejected the boot config."
        case .unavailable:
            return "Orlix boot handoff is not wired to iOS-hosted Linux execution yet."
        case .unknown:
            return "Orlix bootloader returned an unknown status."
        }
    }
}

public struct OrlixBootConfig: Equatable, Sendable {
    public var profile: OrlixBootProfile
    public var rootImageIdentifier: String
    public var terminalIdentifier: String

    public init(
        profile: OrlixBootProfile,
        rootImageIdentifier: String = "orlix.bundle.rootfs",
        terminalIdentifier: String = "orlix.terminal.main"
    ) {
        self.profile = profile
        self.rootImageIdentifier = rootImageIdentifier
        self.terminalIdentifier = terminalIdentifier
    }
}

public protocol OrlixTerminalInput: AnyObject {
    func send(_ data: Data)
}

protocol OrlixTerminalTransport: AnyObject {
    func attachOutput(
        _ handler: @escaping @Sendable (Data) -> Void
    ) -> OrlixTerminalOutput
    func send(_ data: Data)
}

public final class OrlixTerminalOutput: @unchecked Sendable {
    private let cancelHandler: @Sendable () -> Void
    private let lock = NSLock()
    private var isCancelled = false

    init(cancel: @escaping @Sendable () -> Void) {
        self.cancelHandler = cancel
    }

    public func cancel() {
        lock.lock()
        let shouldCancel = !isCancelled
        isCancelled = true
        lock.unlock()

        if shouldCancel {
            cancelHandler()
        }
    }

    deinit {
        cancel()
    }
}

public final class OrlixTerminalSession: OrlixTerminalInput, @unchecked Sendable {
    private let transport: OrlixTerminalTransport

    public convenience init() {
        self.init(transport: HostConsoleTerminalTransport())
    }

    init(transport: OrlixTerminalTransport) {
        self.transport = transport
    }

    @discardableResult
    public func attachOutput(
        _ handler: @escaping @Sendable (Data) -> Void
    ) -> OrlixTerminalOutput {
        transport.attachOutput(handler)
    }

    public func send(_ data: Data) {
        transport.send(data)
    }
}

public final class OrlixLinuxSession: @unchecked Sendable {
    public let terminal: OrlixTerminalSession
    public let bootConfig: OrlixBootConfig

    public init(
        bootConfig: OrlixBootConfig,
        terminal: OrlixTerminalSession = OrlixTerminalSession()
    ) {
        self.bootConfig = bootConfig
        self.terminal = terminal
    }

    public func boot() -> OrlixBootStatus {
        bootConfig.rootImageIdentifier.withCString { rootImageIdentifier in
            bootConfig.terminalIdentifier.withCString { terminalIdentifier in
                var cConfig = COrlixBootConfig(
                    profile: bootConfig.profile.cValue,
                    rootImageIdentifier: rootImageIdentifier,
                    terminalIdentifier: terminalIdentifier
                )
                return OrlixBootStatus(rawStatus: OrlixBoot(&cConfig))
            }
        }
    }
}

private final class HostConsoleTerminalTransport:
    OrlixTerminalTransport,
    @unchecked Sendable
{
    private let pipe = Pipe()
    private let lock = NSLock()
    private var outputHandlers: [UUID: @Sendable (Data) -> Void] = [:]

    init() {
        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty else {
                return
            }
            self?.emit(data)
        }
        orlix_host_console_set_output_fd(
            pipe.fileHandleForWriting.fileDescriptor
        )
    }

    deinit {
        pipe.fileHandleForReading.readabilityHandler = nil
        orlix_host_console_set_output_fd(-1)
    }

    func attachOutput(
        _ handler: @escaping @Sendable (Data) -> Void
    ) -> OrlixTerminalOutput {
        let id = UUID()
        lock.lock()
        outputHandlers[id] = handler
        lock.unlock()

        return OrlixTerminalOutput { [weak self] in
            self?.removeOutputHandler(id)
        }
    }

    func send(_ data: Data) {
        data.withUnsafeBytes { buffer in
            guard let baseAddress = buffer.baseAddress else {
                return
            }
            _ = orlix_host_console_enqueue_input(
                baseAddress,
                UInt(buffer.count)
            )
        }
    }

    private func emit(_ data: Data) {
        lock.lock()
        let handlers = Array(outputHandlers.values)
        lock.unlock()

        for handler in handlers {
            handler(data)
        }
    }

    private func removeOutputHandler(_ id: UUID) {
        lock.lock()
        outputHandlers[id] = nil
        lock.unlock()
    }
}
