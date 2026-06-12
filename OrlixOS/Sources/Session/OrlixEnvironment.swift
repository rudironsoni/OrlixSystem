import Foundation

@_spi(OrlixPrivateTesting)
public enum OrlixEnvironmentSource: Codable, Equatable, Sendable {
    case defaultRoot
    case copiedEnvironment(parentID: String)
    case rootfsTar
    case ociLayout
}

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentDescriptor: Codable, Equatable, Sendable {
    public static let defaultEnvironmentID = "default"

    public let id: String
    public let source: OrlixEnvironmentSource
    public let platform: String
    public let rootImageIdentifier: String
    public let defaultCommand: [String]
    public let defaultEnvironment: [String: String]
    public let defaultWorkingDirectory: String
    public let defaultUserID: UInt32
    public let defaultGroupID: UInt32
    public let rootMount: OrlixEnvironmentRootMount
    public let mounts: [OrlixEnvironmentMount]

    public static func defaultEnvironment(
        rootImageIdentifier: String =
            OrlixOSDistribution.productRootImageIdentifier ?? ""
    ) -> OrlixEnvironmentDescriptor {
        OrlixEnvironmentDescriptor(
            id: defaultEnvironmentID,
            source: .defaultRoot,
            platform: "linux/arm64",
            rootImageIdentifier: rootImageIdentifier,
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: [
                "HOME": "/root",
                "PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
                "TERM": "xterm-256color"
            ],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0,
            rootMount: .defaultOverlay,
            mounts: []
        )
    }

    public init(
        id: String,
        source: OrlixEnvironmentSource,
        platform: String,
        rootImageIdentifier: String,
        defaultCommand: [String],
        defaultEnvironment: [String: String],
        defaultWorkingDirectory: String,
        defaultUserID: UInt32,
        defaultGroupID: UInt32,
        rootMount: OrlixEnvironmentRootMount = .defaultOverlay,
        mounts: [OrlixEnvironmentMount] = []
    ) {
        self.id = id
        self.source = source
        self.platform = platform
        self.rootImageIdentifier = rootImageIdentifier
        self.defaultCommand = defaultCommand
        self.defaultEnvironment = defaultEnvironment
        self.defaultWorkingDirectory = defaultWorkingDirectory
        self.defaultUserID = defaultUserID
        self.defaultGroupID = defaultGroupID
        self.rootMount = rootMount
        self.mounts = mounts
    }

    private enum CodingKeys: String, CodingKey {
        case id
        case source
        case platform
        case rootImageIdentifier
        case defaultCommand
        case defaultEnvironment
        case defaultWorkingDirectory
        case defaultUserID
        case defaultGroupID
        case rootMount
        case mounts
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.id = try container.decode(String.self, forKey: .id)
        self.source = try container.decode(
            OrlixEnvironmentSource.self,
            forKey: .source
        )
        self.platform = try container.decode(String.self, forKey: .platform)
        self.rootImageIdentifier = try container.decode(
            String.self,
            forKey: .rootImageIdentifier
        )
        self.defaultCommand = try container.decode(
            [String].self,
            forKey: .defaultCommand
        )
        self.defaultEnvironment = try container.decode(
            [String: String].self,
            forKey: .defaultEnvironment
        )
        self.defaultWorkingDirectory = try container.decode(
            String.self,
            forKey: .defaultWorkingDirectory
        )
        self.defaultUserID = try container.decode(
            UInt32.self,
            forKey: .defaultUserID
        )
        self.defaultGroupID = try container.decode(
            UInt32.self,
            forKey: .defaultGroupID
        )
        self.rootMount = try container.decodeIfPresent(
            OrlixEnvironmentRootMount.self,
            forKey: .rootMount
        ) ?? .defaultOverlay
        self.mounts = try container.decodeIfPresent(
            [OrlixEnvironmentMount].self,
            forKey: .mounts
        ) ?? []
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(id, forKey: .id)
        try container.encode(source, forKey: .source)
        try container.encode(platform, forKey: .platform)
        try container.encode(rootImageIdentifier, forKey: .rootImageIdentifier)
        try container.encode(defaultCommand, forKey: .defaultCommand)
        try container.encode(defaultEnvironment, forKey: .defaultEnvironment)
        try container.encode(defaultWorkingDirectory, forKey: .defaultWorkingDirectory)
        try container.encode(defaultUserID, forKey: .defaultUserID)
        try container.encode(defaultGroupID, forKey: .defaultGroupID)
        try container.encode(rootMount, forKey: .rootMount)
        try container.encode(mounts, forKey: .mounts)
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixEnvironmentMountSource: Codable, Equatable, Sendable {
    case documents
    case securityScopedExternal(bookmarkID: String)
}

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentMount: Codable, Equatable, Sendable {
    public let source: OrlixEnvironmentMountSource
    public let targetPath: String
    public let readOnly: Bool

    public static func documents(
        targetPath: String,
        readOnly: Bool = false
    ) throws -> OrlixEnvironmentMount {
        try validateLinuxMountTarget(targetPath)
        return OrlixEnvironmentMount(
            source: .documents,
            targetPath: targetPath,
            readOnly: readOnly
        )
    }

    public static func securityScopedExternal(
        bookmarkID: String,
        targetPath: String,
        readOnly: Bool = false
    ) throws -> OrlixEnvironmentMount {
        try validateSecurityScopedBookmarkID(bookmarkID)
        try validateLinuxMountTarget(targetPath)
        return OrlixEnvironmentMount(
            source: .securityScopedExternal(bookmarkID: bookmarkID),
            targetPath: targetPath,
            readOnly: readOnly
        )
    }

    private init(
        source: OrlixEnvironmentMountSource,
        targetPath: String,
        readOnly: Bool
    ) {
        self.source = source
        self.targetPath = targetPath
        self.readOnly = readOnly
    }

    private enum CodingKeys: String, CodingKey {
        case source
        case targetPath
        case readOnly
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let source = try container.decode(
            OrlixEnvironmentMountSource.self,
            forKey: .source
        )
        let targetPath = try container.decode(String.self, forKey: .targetPath)
        let readOnly = try container.decode(Bool.self, forKey: .readOnly)

        switch source {
        case .documents:
            self = try .documents(targetPath: targetPath, readOnly: readOnly)
        case let .securityScopedExternal(bookmarkID):
            self = try .securityScopedExternal(
                bookmarkID: bookmarkID,
                targetPath: targetPath,
                readOnly: readOnly
            )
        }
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(source, forKey: .source)
        try container.encode(targetPath, forKey: .targetPath)
        try container.encode(readOnly, forKey: .readOnly)
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixEnvironmentMountError: Error, Equatable, Sendable {
    case invalidSourceIdentifier(String)
    case invalidTargetPath(String)
    case reservedTargetPath(String)
}

private func validateSecurityScopedBookmarkID(_ bookmarkID: String) throws {
    guard !bookmarkID.isEmpty,
          !bookmarkID.contains("/"),
          !bookmarkID.contains("\\"),
          !bookmarkID.contains("\u{0}"),
          bookmarkID != ".",
          bookmarkID != "..",
          !bookmarkID.hasPrefix("."),
          !bookmarkID.contains("..")
    else {
        throw OrlixEnvironmentMountError.invalidSourceIdentifier(bookmarkID)
    }
}

private func validateLinuxMountTarget(_ targetPath: String) throws {
    guard targetPath.hasPrefix("/"),
          !targetPath.contains("\u{0}"),
          !targetPath.contains("//"),
          !targetPath.split(separator: "/").contains("..")
    else {
        throw OrlixEnvironmentMountError.invalidTargetPath(targetPath)
    }

    let reservedTargets = [
        "/",
        "/dev",
        "/proc",
        "/run",
        "/sys",
        "/tmp"
    ]
    if reservedTargets.contains(targetPath) ||
        reservedTargets.contains(where: { targetPath.hasPrefix($0 + "/") }) {
        throw OrlixEnvironmentMountError.reservedTargetPath(targetPath)
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentRootMount: Codable, Equatable, Sendable {
    public let baseDevicePath: String
    public let stateDevicePath: String
    public let lowerMountPath: String
    public let stateMountPath: String
    public let overlayMountPath: String
    public let upperDirectoryPath: String
    public let workDirectoryPath: String
    public let finalRootPath: String
    public let filesystemType: String
    public let overlayFilesystemType: String
    public let baseReadOnly: Bool

    public static let defaultOverlay = OrlixEnvironmentRootMount(
        baseDevicePath: "/dev/vda",
        stateDevicePath: "/dev/vdb",
        lowerMountPath: "/lower",
        stateMountPath: "/state",
        overlayMountPath: "/newroot",
        upperDirectoryPath: "/state/upper",
        workDirectoryPath: "/state/work",
        finalRootPath: "/",
        filesystemType: "ext4",
        overlayFilesystemType: "overlay",
        baseReadOnly: true
    )

    public init(
        baseDevicePath: String,
        stateDevicePath: String,
        lowerMountPath: String,
        stateMountPath: String,
        overlayMountPath: String,
        upperDirectoryPath: String,
        workDirectoryPath: String,
        finalRootPath: String,
        filesystemType: String,
        overlayFilesystemType: String,
        baseReadOnly: Bool
    ) {
        self.baseDevicePath = baseDevicePath
        self.stateDevicePath = stateDevicePath
        self.lowerMountPath = lowerMountPath
        self.stateMountPath = stateMountPath
        self.overlayMountPath = overlayMountPath
        self.upperDirectoryPath = upperDirectoryPath
        self.workDirectoryPath = workDirectoryPath
        self.finalRootPath = finalRootPath
        self.filesystemType = filesystemType
        self.overlayFilesystemType = overlayFilesystemType
        self.baseReadOnly = baseReadOnly
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentStorageLayout: Equatable, Sendable {
    public let environmentID: String
    public let rootDirectory: URL
    public let baseImageURL: URL
    public let stateImageURL: URL
    public let importScratchDirectory: URL
    public let downloadCacheDirectory: URL

    public static func layout(
        forEnvironmentID environmentID: String,
        policy: OrlixStoragePolicy = .current,
        fileManager: FileManager = .default
    ) throws -> OrlixEnvironmentStorageLayout {
        let storageID = try storageSafeID(environmentID)
        let linuxStateRoot = try policy.linuxStateDirectory(fileManager: fileManager)
        let cacheRoot = try policy.cacheDirectory(fileManager: fileManager)
        let scratchRoot = policy.scratchDirectory(fileManager: fileManager)
        return layout(
            forEnvironmentID: environmentID,
            storageID: storageID,
            linuxStateRoot: linuxStateRoot,
            cacheRoot: cacheRoot,
            scratchRoot: scratchRoot
        )
    }

    public static func layout(
        forEnvironmentID environmentID: String,
        linuxStateRoot: URL,
        cacheRoot: URL,
        scratchRoot: URL
    ) throws -> OrlixEnvironmentStorageLayout {
        try layout(
            forEnvironmentID: environmentID,
            storageID: storageSafeID(environmentID),
            linuxStateRoot: linuxStateRoot,
            cacheRoot: cacheRoot,
            scratchRoot: scratchRoot
        )
    }

    private static func layout(
        forEnvironmentID environmentID: String,
        storageID: String,
        linuxStateRoot: URL,
        cacheRoot: URL,
        scratchRoot: URL
    ) -> OrlixEnvironmentStorageLayout {
        let environmentRoot = linuxStateRoot
            .appendingPathComponent("environments", isDirectory: true)
            .appendingPathComponent(storageID, isDirectory: true)

        return OrlixEnvironmentStorageLayout(
            environmentID: environmentID,
            rootDirectory: environmentRoot,
            baseImageURL: environmentRoot
                .appendingPathComponent("base.ext4", isDirectory: false),
            stateImageURL: environmentRoot
                .appendingPathComponent("state.ext4", isDirectory: false),
            importScratchDirectory: scratchRoot
                .appendingPathComponent("imports", isDirectory: true)
                .appendingPathComponent(storageID, isDirectory: true),
            downloadCacheDirectory: cacheRoot
                .appendingPathComponent("downloads", isDirectory: true)
        )
    }

    private static func storageSafeID(_ id: String) throws -> String {
        guard !id.isEmpty,
              !id.contains("/"),
              !id.contains("\\"),
              !id.utf8.contains(0)
        else {
            throw OrlixEnvironmentStorageLayoutError.invalidEnvironmentID(id)
        }

        var result = ""
        result.reserveCapacity(id.count)
        for character in id.unicodeScalars {
            switch character.value {
            case 48...57, 65...90, 97...122:
                result.unicodeScalars.append(character)
            case 45, 46, 95:
                result.unicodeScalars.append(character)
            default:
                result.append("-")
            }
        }

        guard result != "." && result != "..",
              !result.hasPrefix("."),
              !result.contains(".."),
              !result.contains("/")
        else {
            throw OrlixEnvironmentStorageLayoutError.invalidEnvironmentID(id)
        }
        return result
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixEnvironmentStorageLayoutError:
    Error,
    Equatable,
    Sendable
{
    case invalidEnvironmentID(String)
}

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentRootImage: Equatable, Sendable {
    public static let defaultKernelCommandLine =
        "console=ttyS0 console=hvc0 rdinit=/init orlix.root=overlay orlix.profile=development"
    public static let defaultExecCommandLineKey = "orlix.exec"
    public static let defaultArgumentCommandLineKeyPrefix = "orlix.argv"
    public static let defaultEnvironmentCommandLineKeyPrefix = "orlix.env"
    public static let defaultWorkingDirectoryCommandLineKey = "orlix.cwd"
    public static let defaultUserIDCommandLineKey = "orlix.uid"
    public static let defaultGroupIDCommandLineKey = "orlix.gid"

    public let environmentID: String
    public let rootImageIdentifier: String
    public let baseImageURL: URL
    public let stateImageURL: URL
    public let bootConfig: OrlixBootConfig

    public static func materialized(
        descriptor: OrlixEnvironmentDescriptor,
        layout: OrlixEnvironmentStorageLayout,
        bootProfile: OrlixBootProfile = .development,
        kernelCommandLine: String? = defaultKernelCommandLine,
        fileManager: FileManager = .default
    ) throws -> OrlixEnvironmentRootImage {
        guard descriptor.id == layout.environmentID else {
            throw OrlixEnvironmentRootImageError.environmentMismatch(
                descriptorID: descriptor.id,
                layoutID: layout.environmentID
            )
        }
        try validateImageURL(
            layout.baseImageURL,
            expectedRoot: layout.rootDirectory,
            fileManager: fileManager
        )
        try validateImageURL(
            layout.stateImageURL,
            expectedRoot: layout.rootDirectory,
            fileManager: fileManager
        )
        try validateSupportedMounts(descriptor.mounts)
        let resolvedCommandLine = try materializedKernelCommandLine(
            descriptor: descriptor,
            kernelCommandLine: kernelCommandLine
        )
        return OrlixEnvironmentRootImage(
            environmentID: descriptor.id,
            rootImageIdentifier: descriptor.rootImageIdentifier,
            baseImageURL: layout.baseImageURL,
            stateImageURL: layout.stateImageURL,
            bootConfig: OrlixBootConfig(
                profile: bootProfile,
                kernelCommandLine: resolvedCommandLine,
                rootImageIdentifier: descriptor.rootImageIdentifier
            )
        )
    }

    public func registerWithHostAdapter() -> Bool {
        OrlixOSPayload.registerMaterializedRootImage(self)
    }

    @_spi(OrlixPrivateTesting)
    public func registerWithHostAdapterForTesting(
        payloadBundlePath: String,
        initrdResource: String,
        baseBlockDevice: UInt32,
        stateBlockDevice: UInt32,
        stateBlockMinimumBytes: UInt64
    ) -> Bool {
        OrlixOSPayload.registerMaterializedRootImage(
            self,
            payloadBundlePath: payloadBundlePath,
            productResources: OrlixOSPayload.ProductRootResources(
                initrdResource: initrdResource,
                baseBlockResource: "",
                stateBlockResource: "",
                baseBlockDevice: baseBlockDevice,
                stateBlockDevice: stateBlockDevice,
                stateBlockMinimumBytes: stateBlockMinimumBytes
            )
        )
    }

    private static func validateImageURL(
        _ url: URL,
        expectedRoot: URL,
        fileManager: FileManager
    ) throws {
        let root = expectedRoot.standardizedFileURL
        let image = url.standardizedFileURL
        let rootPath = root.path.hasSuffix("/") ? root.path : root.path + "/"
        guard image.path.hasPrefix(rootPath) else {
            throw OrlixEnvironmentRootImageError.imageOutsideEnvironmentRoot(
                image.path
            )
        }

        var isDirectory = ObjCBool(false)
        guard fileManager.fileExists(
            atPath: image.path,
            isDirectory: &isDirectory
        ) else {
            throw OrlixEnvironmentRootImageError.missingImage(image.path)
        }
        if isDirectory.boolValue {
            throw OrlixEnvironmentRootImageError.imageIsDirectory(image.path)
        }
    }

    private static func materializedKernelCommandLine(
        descriptor: OrlixEnvironmentDescriptor,
        kernelCommandLine: String?
    ) throws -> String? {
        guard kernelCommandLine == defaultKernelCommandLine else {
            return kernelCommandLine
        }

        guard let command = descriptor.defaultCommand.first else {
            throw OrlixEnvironmentRootImageError.invalidDefaultCommand("")
        }
        try validateExecCommand(command)
        let executionTokens = try materializedExecutionTokens(descriptor)
        return ([defaultKernelCommandLine] + executionTokens).joined(separator: " ")
    }

    private static func validateExecCommand(_ command: String) throws {
        guard !command.isEmpty,
              !command.contains("\u{0}"),
              command.hasPrefix("/") || !command.contains("/")
        else {
            throw OrlixEnvironmentRootImageError.invalidDefaultCommand(command)
        }
    }

    private static func validateArgument(_ argument: String) throws {
        guard !argument.contains("\u{0}")
        else {
            throw OrlixEnvironmentRootImageError.invalidDefaultArgument(argument)
        }
    }

    private static func validateEnvironmentEntry(
        key: String,
        value: String
    ) throws -> String {
        guard !key.isEmpty,
              !key.contains("="),
              !key.contains("\u{0}"),
              !value.contains("\u{0}")
        else {
            throw OrlixEnvironmentRootImageError.invalidDefaultEnvironment(key)
        }
        return "\(key)=\(value)"
    }

    private static func validateWorkingDirectory(_ path: String) throws {
        guard path.hasPrefix("/"),
              !path.isEmpty,
              !path.contains("\u{0}")
        else {
            throw OrlixEnvironmentRootImageError.invalidDefaultWorkingDirectory(path)
        }
    }

    private static func validateSupportedMounts(
        _ mounts: [OrlixEnvironmentMount]
    ) throws {
        if let mount = mounts.first {
            throw OrlixEnvironmentRootImageError.missingLinuxMountBackend(mount)
        }
    }

    private static func materializedExecutionTokens(
        _ descriptor: OrlixEnvironmentDescriptor
    ) throws -> [String] {
        var tokens = [
            "\(defaultExecCommandLineKey)=\(percentEncoded(descriptor.defaultCommand[0]))"
        ]
        for (index, argument) in descriptor.defaultCommand.enumerated() {
            try validateArgument(argument)
            tokens.append(
                "\(defaultArgumentCommandLineKeyPrefix)\(index)=\(percentEncoded(argument))"
            )
        }
        for (index, entry) in descriptor.defaultEnvironment
            .sorted(by: { $0.key < $1.key })
            .enumerated()
        {
            let assignment = try validateEnvironmentEntry(
                key: entry.key,
                value: entry.value
            )
            tokens.append(
                "\(defaultEnvironmentCommandLineKeyPrefix)\(index)=\(percentEncoded(assignment))"
            )
        }
        try validateWorkingDirectory(descriptor.defaultWorkingDirectory)
        tokens.append(
            "\(defaultWorkingDirectoryCommandLineKey)=\(percentEncoded(descriptor.defaultWorkingDirectory))"
        )
        tokens.append("\(defaultUserIDCommandLineKey)=\(descriptor.defaultUserID)")
        tokens.append("\(defaultGroupIDCommandLineKey)=\(descriptor.defaultGroupID)")
        return tokens
    }

    private static func percentEncoded(_ value: String) -> String {
        var encoded = ""
        for byte in value.utf8 {
            if isKernelCommandLineTokenByte(byte) {
                encoded.append(Character(UnicodeScalar(byte)))
            } else {
                encoded += String(format: "%%%02X", byte)
            }
        }
        return encoded
    }

    private static func isKernelCommandLineTokenByte(_ byte: UInt8) -> Bool {
        switch byte {
        case UInt8(ascii: "A")...UInt8(ascii: "Z"),
             UInt8(ascii: "a")...UInt8(ascii: "z"),
             UInt8(ascii: "0")...UInt8(ascii: "9"),
             UInt8(ascii: "/"),
             UInt8(ascii: "."),
             UInt8(ascii: "_"),
             UInt8(ascii: "-"),
             UInt8(ascii: ":"),
             UInt8(ascii: "="),
             UInt8(ascii: ","),
             UInt8(ascii: "+"),
             UInt8(ascii: "@"):
            return true
        default:
            return false
        }
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixEnvironmentRootImageError:
    Error,
    Equatable,
    Sendable
{
    case environmentMismatch(descriptorID: String, layoutID: String)
    case imageOutsideEnvironmentRoot(String)
    case missingImage(String)
    case imageIsDirectory(String)
    case invalidDefaultCommand(String)
    case invalidDefaultArgument(String)
    case invalidDefaultEnvironment(String)
    case invalidDefaultWorkingDirectory(String)
    case missingLinuxMountBackend(OrlixEnvironmentMount)
}

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentRegistry: Sendable {
    public let linuxStateRoot: URL
    public let cacheRoot: URL
    public let scratchRoot: URL

    public init(
        policy: OrlixStoragePolicy = .current,
        fileManager: FileManager = .default
    ) throws {
        self.init(
            linuxStateRoot: try policy.linuxStateDirectory(
                fileManager: fileManager
            ),
            cacheRoot: try policy.cacheDirectory(fileManager: fileManager),
            scratchRoot: policy.scratchDirectory(fileManager: fileManager)
        )
    }

    public init(
        linuxStateRoot: URL,
        cacheRoot: URL,
        scratchRoot: URL
    ) {
        self.linuxStateRoot = linuxStateRoot
        self.cacheRoot = cacheRoot
        self.scratchRoot = scratchRoot
    }

    public func layout(
        forEnvironmentID environmentID: String
    ) throws -> OrlixEnvironmentStorageLayout {
        try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: environmentID,
            linuxStateRoot: linuxStateRoot,
            cacheRoot: cacheRoot,
            scratchRoot: scratchRoot
        )
    }

    public func descriptorURL(
        forEnvironmentID environmentID: String
    ) throws -> URL {
        try layout(forEnvironmentID: environmentID).rootDirectory
            .appendingPathComponent("environment.json", isDirectory: false)
    }

    public func prepareStorage(
        forEnvironmentID environmentID: String,
        fileManager: FileManager = .default
    ) throws -> OrlixEnvironmentStorageLayout {
        let layout = try layout(forEnvironmentID: environmentID)
        try fileManager.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try fileManager.createDirectory(
            at: layout.importScratchDirectory,
            withIntermediateDirectories: true
        )
        try fileManager.createDirectory(
            at: layout.downloadCacheDirectory,
            withIntermediateDirectories: true
        )
        return layout
    }

    public func prepareStorage(
        for descriptor: OrlixEnvironmentDescriptor,
        fileManager: FileManager = .default
    ) throws -> OrlixEnvironmentStorageLayout {
        try prepareStorage(
            forEnvironmentID: descriptor.id,
            fileManager: fileManager
        )
    }

    public func save(
        _ descriptor: OrlixEnvironmentDescriptor,
        fileManager: FileManager = .default
    ) throws {
        let layout = try prepareStorage(
            for: descriptor,
            fileManager: fileManager
        )
        let data = try JSONEncoder.orlixEnvironmentEncoder.encode(descriptor)
        try data.write(
            to: layout.rootDirectory
                .appendingPathComponent("environment.json", isDirectory: false),
            options: [.atomic]
        )
    }

    public func load(
        environmentID: String,
        fileManager: FileManager = .default
    ) throws -> OrlixEnvironmentDescriptor {
        let url = try descriptorURL(forEnvironmentID: environmentID)
        let data = try Data(contentsOf: url)
        return try JSONDecoder().decode(OrlixEnvironmentDescriptor.self, from: data)
    }

    public func list(
        fileManager: FileManager = .default
    ) throws -> [OrlixEnvironmentDescriptor] {
        let environmentsRoot = linuxStateRoot
            .appendingPathComponent("environments", isDirectory: true)
        guard let contents = try? fileManager.contentsOfDirectory(
            at: environmentsRoot,
            includingPropertiesForKeys: [.isDirectoryKey],
            options: [.skipsHiddenFiles]
        ) else {
            return []
        }

        var descriptors: [OrlixEnvironmentDescriptor] = []
        for directory in contents.sorted(by: { $0.path < $1.path }) {
            let descriptorURL = directory.appendingPathComponent(
                "environment.json",
                isDirectory: false
            )
            guard fileManager.fileExists(atPath: descriptorURL.path) else {
                continue
            }
            let data = try Data(contentsOf: descriptorURL)
            descriptors.append(
                try JSONDecoder().decode(
                    OrlixEnvironmentDescriptor.self,
                    from: data
                )
            )
        }
        return descriptors
    }

    public func materializedRootImage(
        forEnvironmentID environmentID: String,
        kernelCommandLine: String? = OrlixEnvironmentRootImage.defaultKernelCommandLine,
        fileManager: FileManager = .default
    ) throws -> OrlixEnvironmentRootImage {
        try OrlixEnvironmentRootImage.materialized(
            descriptor: load(environmentID: environmentID, fileManager: fileManager),
            layout: layout(forEnvironmentID: environmentID),
            kernelCommandLine: kernelCommandLine,
            fileManager: fileManager
        )
    }

    public func copyEnvironment(
        from parentID: String,
        to environmentID: String,
        rootImageIdentifier: String,
        fileManager: FileManager = .default
    ) throws -> OrlixEnvironmentDescriptor {
        let parent = try load(environmentID: parentID, fileManager: fileManager)
        let parentLayout = try layout(forEnvironmentID: parentID)
        let destinationLayout = try layout(forEnvironmentID: environmentID)

        guard fileManager.fileExists(atPath: parentLayout.baseImageURL.path) else {
            throw OrlixEnvironmentCopyError.missingParentImage(
                parentLayout.baseImageURL.path
            )
        }
        guard fileManager.fileExists(atPath: parentLayout.stateImageURL.path) else {
            throw OrlixEnvironmentCopyError.missingParentImage(
                parentLayout.stateImageURL.path
            )
        }
        guard !fileManager.fileExists(atPath: destinationLayout.rootDirectory.path)
        else {
            throw OrlixEnvironmentCopyError.destinationExists(environmentID)
        }

        try fileManager.createDirectory(
            at: destinationLayout.rootDirectory,
            withIntermediateDirectories: true
        )
        try fileManager.createDirectory(
            at: destinationLayout.importScratchDirectory,
            withIntermediateDirectories: true
        )
        try fileManager.createDirectory(
            at: destinationLayout.downloadCacheDirectory,
            withIntermediateDirectories: true
        )
        do {
            try fileManager.copyItem(
                at: parentLayout.baseImageURL,
                to: destinationLayout.baseImageURL
            )
            try fileManager.copyItem(
                at: parentLayout.stateImageURL,
                to: destinationLayout.stateImageURL
            )
            let descriptor = OrlixEnvironmentDescriptor(
                id: environmentID,
                source: .copiedEnvironment(parentID: parentID),
                platform: parent.platform,
                rootImageIdentifier: rootImageIdentifier,
                defaultCommand: parent.defaultCommand,
                defaultEnvironment: parent.defaultEnvironment,
                defaultWorkingDirectory: parent.defaultWorkingDirectory,
                defaultUserID: parent.defaultUserID,
                defaultGroupID: parent.defaultGroupID,
                rootMount: parent.rootMount,
                mounts: parent.mounts
            )
            try save(descriptor, fileManager: fileManager)
            return descriptor
        } catch {
            try? fileManager.removeItem(at: destinationLayout.rootDirectory)
            throw error
        }
    }
}

private extension JSONEncoder {
    static var orlixEnvironmentEncoder: JSONEncoder {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return encoder
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixEnvironmentCopyError: Error, Equatable, Sendable {
    case destinationExists(String)
    case missingParentImage(String)
}
