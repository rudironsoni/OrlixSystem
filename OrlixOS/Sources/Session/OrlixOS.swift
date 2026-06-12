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

@_silgen_name("orlix_host_resources_set_payload_root_path")
private func orlix_host_resources_set_payload_root_path(
    _ path: UnsafePointer<CChar>
) -> CInt

@_silgen_name("orlix_host_resources_clear_root_images")
private func orlix_host_resources_clear_root_images() -> CInt

@_silgen_name("orlix_host_resources_register_root_image")
private func orlix_host_resources_register_root_image(
    _ identifier: UnsafePointer<CChar>,
    _ initrdBundleName: UnsafePointer<CChar>?,
    _ initrdBundleExtension: UnsafePointer<CChar>?,
    _ initrdResource: UnsafePointer<CChar>,
    _ baseBlockResource: UnsafePointer<CChar>,
    _ stateBlockResource: UnsafePointer<CChar>,
    _ baseBlockDevice: UInt32,
    _ stateBlockDevice: UInt32,
    _ stateBlockMinimumBytes: UInt64
) -> CInt

@_silgen_name("orlix_host_resources_register_root_image_files")
private func orlix_host_resources_register_root_image_files(
    _ identifier: UnsafePointer<CChar>,
    _ initrdBundleName: UnsafePointer<CChar>?,
    _ initrdBundleExtension: UnsafePointer<CChar>?,
    _ initrdResource: UnsafePointer<CChar>,
    _ baseBlockPath: UnsafePointer<CChar>,
    _ stateBlockPath: UnsafePointer<CChar>,
    _ baseBlockDevice: UInt32,
    _ stateBlockDevice: UInt32,
    _ stateBlockMinimumBytes: UInt64
) -> CInt

private struct COrlixBootConfig {
    var profile: CInt
    var kernelCommandLine: UnsafePointer<CChar>?
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
    case alreadyStarted
    case unknown(CInt)

    init(rawStatus: CInt) {
        switch rawStatus {
        case 0:
            self = .ok
        case -1:
            self = .invalidConfig
        case -2:
            self = .unavailable
        case -3:
            self = .alreadyStarted
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
        case .alreadyStarted:
            return "Orlix boot already started in this process."
        case .unknown:
            return "Orlix bootloader returned an unknown status."
        }
    }
}

public struct OrlixBootConfig: Equatable, Sendable {
    public var profile: OrlixBootProfile
    public var kernelCommandLine: String?
    public var rootImageIdentifier: String
    public var terminalIdentifier: String

    public init(
        profile: OrlixBootProfile,
        kernelCommandLine: String? =
            OrlixOSDistribution.bundledKernelCommandLine,
        rootImageIdentifier: String =
            OrlixOSDistribution.productRootImageIdentifier ?? "",
        terminalIdentifier: String = "orlix.terminal.main"
    ) {
        self.profile = profile
        self.kernelCommandLine = kernelCommandLine
        self.rootImageIdentifier = rootImageIdentifier
        self.terminalIdentifier = terminalIdentifier
    }
}

public enum OrlixOSDistribution {
    public static var bundledBootProfile: OrlixBootProfile? {
        OrlixOSPayload.selectedBootProfile
    }

    public static var bundledKernelCommandLine: String? {
        OrlixOSPayload.kernelCommandLine
    }

    public static var productRootImageIdentifier: String? {
        OrlixOSPayload.productRootImageIdentifier
    }

    @_spi(OrlixPrivateTesting)
    public static func rootImageDescriptor(
        forRole role: String
    ) -> OrlixRootImageDescriptor? {
        OrlixOSPayload.rootImageDescriptors.first { $0.role == role }
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixRootImageDescriptor: Equatable, Sendable {
    public let role: String
    public let identifier: String
    public let kernelCommandLine: String?
    public let initrdBundleName: String?
    public let initrdBundleExtension: String?
    public let initrdResource: String?
}

private final class OrlixOSBundleAnchor {}

enum OrlixOSPayload {
    private static let bundleNameKey = "OrlixOSPayloadBundleName"
    private static let bundleExtensionKey = "OrlixOSPayloadBundleExtension"
    private static let payloadProfileInfoKeyKey =
        "OrlixOSPayloadProfileInfoKey"
    private static let payloadKernelCommandLineInfoKeyKey =
        "OrlixOSPayloadKernelCommandLineInfoKey"
    private static let payloadRootInitrdInfoKeyKey =
        "OrlixOSPayloadRootInitramfsInfoKey"
    private static let payloadBaseRootImageInfoKeyKey =
        "OrlixOSPayloadBaseRootImageInfoKey"
    private static let payloadStateRootImageInfoKeyKey =
        "OrlixOSPayloadStateRootImageInfoKey"
    private static let payloadBaseRootDeviceInfoKeyKey =
        "OrlixOSPayloadBaseRootDeviceInfoKey"
    private static let payloadStateRootDeviceInfoKeyKey =
        "OrlixOSPayloadStateRootDeviceInfoKey"
    private static let payloadBaseRootHostBlockDeviceInfoKeyKey =
        "OrlixOSPayloadBaseRootHostBlockDeviceInfoKey"
    private static let payloadStateRootHostBlockDeviceInfoKeyKey =
        "OrlixOSPayloadStateRootHostBlockDeviceInfoKey"
    private static let payloadStateRootMinimumBytesInfoKeyKey =
        "OrlixOSPayloadStateRootMinimumBytesInfoKey"
    private static let productRootImageIdentifierKey =
        "OrlixOSProductRootImageIdentifier"
    private static let rootImageInitrdBundleExtensionKey =
        "OrlixOSRootImageInitrdBundleExtension"
    private static let rootImagesKey = "OrlixOSRootImages"
    private static let rootImageRoleKey = "OrlixRootImageRole"
    private static let rootImageIdentifierKey = "OrlixRootImageIdentifier"
    private static let rootImageKernelCommandLineKey =
        "OrlixRootImageKernelCommandLine"
    private static let rootImageInitrdBundleNameKey =
        "OrlixRootImageInitrdBundleName"
    private static let rootImageInitrdResourceKey =
        "OrlixRootImageInitrdResource"

    private struct PayloadMetadataSchema {
        let selectedProfileKey: String
        let kernelCommandLineKey: String
        let rootInitrdResourceKey: String
        let baseRootImageResourceKey: String
        let stateRootImageResourceKey: String
        let baseRootDeviceKey: String
        let stateRootDeviceKey: String
        let baseRootHostBlockDeviceKey: String
        let stateRootHostBlockDeviceKey: String
        let stateRootMinimumBytesKey: String
    }

    struct ProductRootResources {
        let initrdResource: String
        let baseBlockResource: String
        let stateBlockResource: String
        let baseBlockDevice: UInt32
        let stateBlockDevice: UInt32
        let stateBlockMinimumBytes: UInt64
    }

    static var bundleURL: URL? {
        guard let name = bundleMetadataValue(for: bundleNameKey),
              let extensionName = bundleMetadataValue(for: bundleExtensionKey)
        else {
            return nil
        }

        return frameworkBundle.url(
            forResource: name,
            withExtension: extensionName
        )
    }

    static var selectedBootProfile: OrlixBootProfile? {
        guard let payloadBundleURL = bundleURL,
              let payloadBundle = Bundle(url: payloadBundleURL),
              let schema = payloadMetadataSchema,
              let profile = payloadBundle.object(
                forInfoDictionaryKey: schema.selectedProfileKey
              ) as? String
        else {
            return nil
        }

        switch profile {
        case "release":
            return .release
        case "development":
            return .development
        default:
            return nil
        }
    }

    static var kernelCommandLine: String? {
        guard let payloadBundleURL = bundleURL,
              let payloadBundle = Bundle(url: payloadBundleURL),
              let schema = payloadMetadataSchema
        else {
            return nil
        }

        return payloadMetadataValue(schema.kernelCommandLineKey, in: payloadBundle)
    }

    static var productRootImageIdentifier: String? {
        bundleMetadataValue(for: productRootImageIdentifierKey)
    }

    static var rootImageDescriptors: [OrlixRootImageDescriptor] {
        guard let entries = frameworkBundle.object(
            forInfoDictionaryKey: rootImagesKey
        ) as? [[String: Any]] else {
            return []
        }

        return entries.compactMap { entry in
            guard let role = metadataString(rootImageRoleKey, in: entry),
                  let identifier = metadataString(rootImageIdentifierKey, in: entry)
            else {
                return nil
            }

            let initrdBundleName = metadataString(
                rootImageInitrdBundleNameKey,
                in: entry
            )
            let initrdBundleExtension = initrdBundleName == nil ? nil :
                bundleMetadataValue(for: rootImageInitrdBundleExtensionKey)

            return OrlixRootImageDescriptor(
                role: role,
                identifier: identifier,
                kernelCommandLine: metadataString(
                    rootImageKernelCommandLineKey,
                    in: entry
                ),
                initrdBundleName: initrdBundleName,
                initrdBundleExtension: initrdBundleExtension,
                initrdResource: metadataString(
                    rootImageInitrdResourceKey,
                    in: entry
                )
            )
        }
    }

    static func registerWithHostAdapter() -> Bool {
        guard let payloadBundlePath = bundleURL?.path else {
            return false
        }
        guard let productResources = productResources,
              let productRootImageIdentifier = productRootImageIdentifier,
              rootImageDescriptors.contains(
                where: { $0.identifier == productRootImageIdentifier }
              )
        else {
            return false
        }

        guard payloadBundlePath.withCString({ path in
            orlix_host_resources_set_payload_root_path(path) == 0
        }) else {
            return false
        }
        guard orlix_host_resources_clear_root_images() == 0 else {
            return false
        }

        for descriptor in rootImageDescriptors {
            guard registerRootImage(
                descriptor,
                productResources: productResources
            ) else {
                _ = orlix_host_resources_clear_root_images()
                return false
            }
        }
        return true
    }

    static func registerMaterializedRootImage(
        _ rootImage: OrlixEnvironmentRootImage
    ) -> Bool {
        guard let payloadBundlePath = bundleURL?.path,
              let productResources = productResources
        else {
            return false
        }
        return registerMaterializedRootImage(
            rootImage,
            payloadBundlePath: payloadBundlePath,
            productResources: productResources
        )
    }

    static func registerMaterializedRootImage(
        _ rootImage: OrlixEnvironmentRootImage,
        payloadBundlePath: String,
        productResources: ProductRootResources
    ) -> Bool {
        guard payloadBundlePath.withCString({ path in
            orlix_host_resources_set_payload_root_path(path) == 0
        }) else {
            return false
        }
        guard orlix_host_resources_clear_root_images() == 0 else {
            return false
        }

        let initrdBundleName = ""
        let initrdBundleExtension = ""
        let registered = rootImage.rootImageIdentifier.withCString { identifier in
            initrdBundleName.withCString { bundleName in
                initrdBundleExtension.withCString { bundleExtension in
                    productResources.initrdResource.withCString { initrd in
                        rootImage.baseImageURL.path.withCString { base in
                            rootImage.stateImageURL.path.withCString { state in
                                orlix_host_resources_register_root_image_files(
                                    identifier,
                                    bundleName,
                                    bundleExtension,
                                    initrd,
                                    base,
                                    state,
                                    productResources.baseBlockDevice,
                                    productResources.stateBlockDevice,
                                    productResources.stateBlockMinimumBytes
                                ) == 0
                            }
                        }
                    }
                }
            }
        }
        if !registered {
            _ = orlix_host_resources_clear_root_images()
        }
        return registered
    }

    private static var productResources: ProductRootResources? {
        guard let payloadBundleURL = bundleURL,
              let payloadBundle = Bundle(url: payloadBundleURL),
              let schema = payloadMetadataSchema,
              let initrdResource = payloadMetadataValue(
                schema.rootInitrdResourceKey,
                in: payloadBundle
              ),
              let baseBlockResource = payloadMetadataValue(
                schema.baseRootImageResourceKey,
                in: payloadBundle
              ),
              let stateBlockResource = payloadMetadataValue(
                schema.stateRootImageResourceKey,
                in: payloadBundle
              ),
              payloadMetadataValue(schema.baseRootDeviceKey, in: payloadBundle) != nil,
              payloadMetadataValue(schema.stateRootDeviceKey, in: payloadBundle) != nil,
              let baseBlockDevice = payloadMetadataUInt32(
                schema.baseRootHostBlockDeviceKey,
                in: payloadBundle
              ),
              let stateBlockDevice = payloadMetadataUInt32(
                schema.stateRootHostBlockDeviceKey,
                in: payloadBundle
              ),
              let stateBlockMinimumBytes = payloadMetadataUInt64(
                schema.stateRootMinimumBytesKey,
                in: payloadBundle
              )
        else {
            return nil
        }

        return ProductRootResources(
            initrdResource: initrdResource,
            baseBlockResource: baseBlockResource,
            stateBlockResource: stateBlockResource,
            baseBlockDevice: baseBlockDevice,
            stateBlockDevice: stateBlockDevice,
            stateBlockMinimumBytes: stateBlockMinimumBytes
        )
    }

    private static var payloadMetadataSchema: PayloadMetadataSchema? {
        guard let selectedProfileKey = bundleMetadataValue(
                for: payloadProfileInfoKeyKey
              ),
              let kernelCommandLineKey = bundleMetadataValue(
                for: payloadKernelCommandLineInfoKeyKey
              ),
              let rootInitrdResourceKey = bundleMetadataValue(
                for: payloadRootInitrdInfoKeyKey
              ),
              let baseRootImageResourceKey = bundleMetadataValue(
                for: payloadBaseRootImageInfoKeyKey
              ),
              let stateRootImageResourceKey = bundleMetadataValue(
                for: payloadStateRootImageInfoKeyKey
              ),
              let baseRootDeviceKey = bundleMetadataValue(
                for: payloadBaseRootDeviceInfoKeyKey
              ),
              let stateRootDeviceKey = bundleMetadataValue(
                for: payloadStateRootDeviceInfoKeyKey
              ),
              let baseRootHostBlockDeviceKey = bundleMetadataValue(
                for: payloadBaseRootHostBlockDeviceInfoKeyKey
              ),
              let stateRootHostBlockDeviceKey = bundleMetadataValue(
                for: payloadStateRootHostBlockDeviceInfoKeyKey
              ),
              let stateRootMinimumBytesKey = bundleMetadataValue(
                for: payloadStateRootMinimumBytesInfoKeyKey
              )
        else {
            return nil
        }

        return PayloadMetadataSchema(
            selectedProfileKey: selectedProfileKey,
            kernelCommandLineKey: kernelCommandLineKey,
            rootInitrdResourceKey: rootInitrdResourceKey,
            baseRootImageResourceKey: baseRootImageResourceKey,
            stateRootImageResourceKey: stateRootImageResourceKey,
            baseRootDeviceKey: baseRootDeviceKey,
            stateRootDeviceKey: stateRootDeviceKey,
            baseRootHostBlockDeviceKey: baseRootHostBlockDeviceKey,
            stateRootHostBlockDeviceKey: stateRootHostBlockDeviceKey,
            stateRootMinimumBytesKey: stateRootMinimumBytesKey
        )
    }

    private static var frameworkBundle: Bundle {
        Bundle(for: OrlixOSBundleAnchor.self)
    }

    private static func bundleMetadataValue(for key: String) -> String? {
        guard let value = frameworkBundle.object(forInfoDictionaryKey: key)
                as? String,
              !value.isEmpty
        else {
            return nil
        }

        return value
    }

    private static func payloadMetadataValue(
        _ key: String,
        in bundle: Bundle
    ) -> String? {
        guard let value = bundle.object(forInfoDictionaryKey: key) as? String,
              !value.isEmpty
        else {
            return nil
        }

        return value
    }

    private static func payloadMetadataUInt32(
        _ key: String,
        in bundle: Bundle
    ) -> UInt32? {
        guard let value = payloadMetadataUInt64(key, in: bundle),
              value <= UInt64(UInt32.max)
        else {
            return nil
        }

        return UInt32(value)
    }

    private static func payloadMetadataUInt64(
        _ key: String,
        in bundle: Bundle
    ) -> UInt64? {
        if let value = bundle.object(forInfoDictionaryKey: key) as? NSNumber {
            let intValue = value.int64Value
            guard intValue >= 0 else {
                return nil
            }
            return UInt64(intValue)
        }
        guard let text = payloadMetadataValue(key, in: bundle) else {
            return nil
        }

        return UInt64(text)
    }

    private static func metadataString(
        _ key: String,
        in metadata: [String: Any]
    ) -> String? {
        guard let value = metadata[key] as? String,
              !value.isEmpty
        else {
            return nil
        }

        return value
    }

    private static func registerRootImage(
        _ descriptor: OrlixRootImageDescriptor,
        productResources: ProductRootResources
    ) -> Bool {
        let initrdBundleName = descriptor.initrdBundleName ?? ""
        let initrdBundleExtension = descriptor.initrdBundleExtension ?? ""
        let initrdResource =
            descriptor.initrdResource ?? productResources.initrdResource

        return descriptor.identifier.withCString { identifier in
            initrdBundleName.withCString { bundleName in
                initrdBundleExtension.withCString { bundleExtension in
                    initrdResource.withCString { initrd in
                        productResources.baseBlockResource.withCString { base in
                            productResources.stateBlockResource.withCString { state in
                                orlix_host_resources_register_root_image(
                                    identifier,
                                    bundleName,
                                    bundleExtension,
                                    initrd,
                                    base,
                                    state,
                                    productResources.baseBlockDevice,
                                    productResources.stateBlockDevice,
                                    productResources.stateBlockMinimumBytes
                                ) == 0
                            }
                        }
                    }
                }
            }
        }
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
    private let materializedRootImage: OrlixEnvironmentRootImage?

    public init(
        bootConfig: OrlixBootConfig,
        terminal: OrlixTerminalSession = OrlixTerminalSession()
    ) {
        self.bootConfig = bootConfig
        self.terminal = terminal
        self.materializedRootImage = nil
    }

    public convenience init(
        environmentID: String,
        terminal: OrlixTerminalSession = OrlixTerminalSession()
    ) throws {
        try self.init(
            environmentID: environmentID,
            registry: OrlixEnvironmentRegistry(),
            terminal: terminal
        )
    }

    @_spi(OrlixPrivateTesting)
    public init(
        materializedRootImage: OrlixEnvironmentRootImage,
        terminal: OrlixTerminalSession = OrlixTerminalSession()
    ) {
        self.bootConfig = materializedRootImage.bootConfig
        self.terminal = terminal
        self.materializedRootImage = materializedRootImage
    }

    @_spi(OrlixPrivateTesting)
    public convenience init(
        environmentID: String,
        registry: OrlixEnvironmentRegistry,
        kernelCommandLine: String? = OrlixOSDistribution.bundledKernelCommandLine,
        terminal: OrlixTerminalSession = OrlixTerminalSession()
    ) throws {
        let rootImage = try registry.materializedRootImage(
            forEnvironmentID: environmentID,
            kernelCommandLine: kernelCommandLine
        )
        self.init(materializedRootImage: rootImage, terminal: terminal)
    }

    public func boot() -> OrlixBootStatus {
        guard registerRootImagesForBoot() else {
            return .invalidConfig
        }

        return bootConfig.rootImageIdentifier.withCString { rootImageIdentifier in
            bootConfig.terminalIdentifier.withCString { terminalIdentifier in
                let boot = { (kernelCommandLine: UnsafePointer<CChar>?) in
                    var cConfig = COrlixBootConfig(
                        profile: self.bootConfig.profile.cValue,
                        kernelCommandLine: kernelCommandLine,
                        rootImageIdentifier: rootImageIdentifier,
                        terminalIdentifier: terminalIdentifier
                    )
                    return OrlixBootStatus(rawStatus: OrlixBoot(&cConfig))
                }

                guard let kernelCommandLine = bootConfig.kernelCommandLine,
                      !kernelCommandLine.isEmpty
                else {
                    return boot(nil)
                }

                return kernelCommandLine.withCString { boot($0) }
            }
        }
    }

    private func registerRootImagesForBoot() -> Bool {
        if let materializedRootImage {
            return materializedRootImage.registerWithHostAdapter()
        }
        return OrlixOSPayload.registerWithHostAdapter()
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
