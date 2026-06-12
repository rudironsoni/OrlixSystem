import Foundation
import zlib

@_spi(OrlixPrivateTesting)
public struct OrlixOCIImageLayoutImport: Equatable, Sendable {
    public let manifestDigest: String
    public let configDigest: String
    public let platform: String
    public let layers: [OrlixOCIImageLayer]
    public let rootfsDiffIDs: [String]
    public let processDefaults: OrlixOCIProcessDefaults
}

@_spi(OrlixPrivateTesting)
public struct OrlixOCIImageLayer: Equatable, Sendable {
    public let digest: String
    public let mediaType: String
    public let size: UInt64
}

@_spi(OrlixPrivateTesting)
public struct OrlixOCIProcessDefaults: Equatable, Sendable {
    public let environment: [String: String]
    public let entrypoint: [String]
    public let command: [String]
    public let workingDirectory: String?
    public let user: String?
}

@_spi(OrlixPrivateTesting)
public struct OrlixOCIImageLayoutReader: Sendable {
    public init() {}

    public func readLayout(
        at layoutURL: URL,
        platform: String = "linux/arm64"
    ) throws -> OrlixOCIImageLayoutImport {
        let requestedPlatform = try OrlixOCIPlatform(platform)
        let layoutData = try Data(
            contentsOf: layoutURL.appendingPathComponent("oci-layout")
        )
        let layout = try JSONDecoder().decode(OCILayout.self, from: layoutData)
        guard layout.imageLayoutVersion == "1.0.0" else {
            throw OrlixOCIImageLayoutError.invalidLayoutVersion(
                layout.imageLayoutVersion
            )
        }

        let indexData = try Data(contentsOf: layoutURL.appendingPathComponent("index.json"))
        let index = try JSONDecoder().decode(OCIIndex.self, from: indexData)
        guard let manifestDescriptor = index.manifests.first(
            where: { $0.platform == requestedPlatform }
        ) else {
            throw OrlixOCIImageLayoutError.missingPlatform(platform)
        }
        guard Self.supportedManifestMediaTypes.contains(manifestDescriptor.mediaType)
        else {
            throw OrlixOCIImageLayoutError.unsupportedManifestMediaType(
                manifestDescriptor.mediaType
            )
        }

        let manifestData = try OrlixOCIImageLayoutBlobStore.verifiedBlob(
            manifestDescriptor.digest,
            expectedSize: manifestDescriptor.size,
            under: layoutURL
        )
        let manifest = try JSONDecoder().decode(OCIManifest.self, from: manifestData)
        guard manifest.schemaVersion == 2 else {
            throw OrlixOCIImageLayoutError.invalidManifestSchemaVersion(
                manifest.schemaVersion
            )
        }
        guard Self.supportedConfigMediaTypes.contains(manifest.config.mediaType)
        else {
            throw OrlixOCIImageLayoutError.unsupportedConfigMediaType(
                manifest.config.mediaType
            )
        }
        let configData = try OrlixOCIImageLayoutBlobStore.verifiedBlob(
            manifest.config.digest,
            expectedSize: manifest.config.size,
            under: layoutURL
        )
        let imageConfig = try JSONDecoder().decode(OCIImageConfig.self, from: configData)
        let layers = try manifest.layers.map { layer -> OrlixOCIImageLayer in
            _ = try OrlixOCIImageLayoutBlobStore.verifiedBlob(
                layer.digest,
                expectedSize: layer.size,
                under: layoutURL
            )
            return OrlixOCIImageLayer(
                digest: layer.digest,
                mediaType: layer.mediaType,
                size: layer.size
            )
        }
        let rootfsDiffIDs = try validatedRootfsDiffIDs(
            imageConfig.rootfs,
            layerCount: layers.count
        )

        return OrlixOCIImageLayoutImport(
            manifestDigest: manifestDescriptor.digest,
            configDigest: manifest.config.digest,
            platform: platform,
            layers: layers,
            rootfsDiffIDs: rootfsDiffIDs,
            processDefaults: OrlixOCIProcessDefaults(
                environment: try environmentDictionary(imageConfig.config?.env ?? []),
                entrypoint: try commandVector(imageConfig.config?.entrypoint ?? []),
                command: try commandVector(imageConfig.config?.cmd ?? []),
                workingDirectory: imageConfig.config?.workingDir,
                user: imageConfig.config?.user
            )
        )
    }

    private func validatedRootfsDiffIDs(
        _ rootfs: OCIRootfs?,
        layerCount: Int
    ) throws -> [String] {
        guard let rootfs else {
            return []
        }
        if let type = rootfs.type, type != "layers" {
            throw OrlixOCIImageLayoutError.unsupportedRootfsType(type)
        }
        let values = rootfs.diffIDs ?? []
        guard !values.isEmpty else {
            return []
        }
        guard values.count == layerCount else {
            throw OrlixOCIImageLayoutError.rootfsDiffIDCountMismatch(
                expected: layerCount,
                actual: values.count
            )
        }
        return try values.map { value in
            let digest = try OrlixOCIDigest(value)
            return "\(digest.algorithm):\(digest.hex)"
        }
    }

    private static let supportedManifestMediaTypes: Set<String> = [
        "application/vnd.oci.image.manifest.v1+json",
        "application/vnd.docker.distribution.manifest.v2+json"
    ]

    private static let supportedConfigMediaTypes: Set<String> = [
        "application/vnd.oci.image.config.v1+json",
        "application/vnd.docker.container.image.v1+json"
    ]

    private func environmentDictionary(_ values: [String]) throws -> [String: String] {
        var result: [String: String] = [:]
        for value in values {
            guard let separator = value.firstIndex(of: "=") else {
                throw OrlixOCIImageLayoutError.invalidEnvironmentEntry(value)
            }
            let key = String(value[..<separator])
            guard !key.isEmpty,
                  !key.contains("\u{0}"),
                  !value.contains("\u{0}")
            else {
                throw OrlixOCIImageLayoutError.invalidEnvironmentEntry(value)
            }
            result[key] = String(value[value.index(after: separator)...])
        }
        return result
    }

    private func commandVector(_ values: [String]) throws -> [String] {
        for value in values {
            guard !value.contains("\u{0}") else {
                throw OrlixOCIImageLayoutError.invalidCommandEntry(value)
            }
        }
        return values
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixOCIImageLayoutImportResult: Equatable, Sendable {
    public let descriptor: OrlixEnvironmentDescriptor
    public let storageLayout: OrlixEnvironmentStorageLayout
    public let stagingRootDirectory: URL
    public let materializationPlan: OrlixEnvironmentImageMaterializationPlan
    public let image: OrlixOCIImageLayoutImport
    public let manifest: [OrlixRootfsTarManifestEntry]
}

@_spi(OrlixPrivateTesting)
public struct OrlixOCIImageLayoutImporter: Sendable {
    public init() {}

    public func importLayout(
        at layoutURL: URL,
        environmentID: String,
        registry: OrlixEnvironmentRegistry,
        rootImageIdentifier: String,
        platform: String = "linux/arm64",
        fileManager: FileManager = .default
    ) throws -> OrlixOCIImageLayoutImportResult {
        let plannedStorageLayout = try registry.layout(forEnvironmentID: environmentID)
        if fileManager.fileExists(atPath: plannedStorageLayout.rootDirectory.path) {
            throw OrlixOCIImageLayoutError.destinationExists(environmentID)
        }
        let image = try OrlixOCIImageLayoutReader().readLayout(
            at: layoutURL,
            platform: platform
        )
        let storageLayout = try registry.prepareStorage(
            forEnvironmentID: environmentID,
            fileManager: fileManager
        )
        var shouldRemoveEnvironmentRootOnFailure = true
        let temporaryRootDirectory = storageLayout.importScratchDirectory
            .appendingPathComponent(
                ".rootfs.oci-import-\(UUID().uuidString)",
                isDirectory: true
            )
        do {
            let stagingRootDirectory = storageLayout.importScratchDirectory
                .appendingPathComponent("rootfs", isDirectory: true)
            try? fileManager.removeItem(at: temporaryRootDirectory)
            try fileManager.createDirectory(
                at: temporaryRootDirectory,
                withIntermediateDirectories: true
            )

            var manifest: [OrlixRootfsTarManifestEntry] = []
            let applicator = OrlixOCIImageLayerApplicator(fileManager: fileManager)
            let decoder = OrlixOCILayerDecoder()
            for (index, layer) in image.layers.enumerated() {
                let data = try OrlixOCIImageLayoutBlobStore.verifiedBlob(
                    layer.digest,
                    expectedSize: layer.size,
                    under: layoutURL
                )
                let tarData = try decoder.decode(
                    layerData: data,
                    mediaType: layer.mediaType
                )
                try verifyDiffID(
                    image.rootfsDiffIDs,
                    layerIndex: index,
                    decodedLayerData: tarData
                )
                let application = try applicator.apply(
                    tarData,
                    to: temporaryRootDirectory
                )
                applyManifestChanges(application, to: &manifest)
            }
            let descriptor = try environmentDescriptor(
                environmentID: environmentID,
                rootImageIdentifier: rootImageIdentifier,
                image: image,
                rootDirectory: temporaryRootDirectory
            )
            if fileManager.fileExists(atPath: stagingRootDirectory.path) {
                try fileManager.removeItem(at: stagingRootDirectory)
            }
            try fileManager.moveItem(
                at: temporaryRootDirectory,
                to: stagingRootDirectory
            )
            let materializationPlan = try OrlixEnvironmentImageMaterializationPlan.plan(
                stagingRootDirectory: stagingRootDirectory,
                storageLayout: storageLayout
            )
            try materializationPlan.prepareInputTrees(fileManager: fileManager)
            try materializationPlan.writeBaseImageMetadataCommands(
                manifest: manifest,
                fileManager: fileManager
            )
            try materializationPlan.writeStateImageMetadataCommands(
                fileManager: fileManager
            )
            try registry.save(descriptor, fileManager: fileManager)
            shouldRemoveEnvironmentRootOnFailure = false
            return OrlixOCIImageLayoutImportResult(
                descriptor: descriptor,
                storageLayout: storageLayout,
                stagingRootDirectory: stagingRootDirectory,
                materializationPlan: materializationPlan,
                image: image,
                manifest: manifest
            )
        } catch {
            try? fileManager.removeItem(at: temporaryRootDirectory)
            if shouldRemoveEnvironmentRootOnFailure {
                try? fileManager.removeItem(at: storageLayout.rootDirectory)
            }
            throw error
        }
    }

    private func applyManifestChanges(
        _ application: OrlixOCIImageLayerApplication,
        to manifest: inout [OrlixRootfsTarManifestEntry]
    ) {
        for operation in application.operations {
            switch operation {
            case let .opaqueDirectory(directory):
                applyOpaqueDirectory(directory, to: &manifest)
            case let .removed(path):
                removePath(path, from: &manifest)
            case let .applied(entry):
                applyEntry(entry, to: &manifest)
            }
        }
    }

    private func applyOpaqueDirectory(
        _ directory: String,
        to manifest: inout [OrlixRootfsTarManifestEntry]
    ) {
        if directory.isEmpty {
            manifest.removeAll()
            return
        }
        let prefix = directory + "/"
        manifest.removeAll { entry in
            entry.path.hasPrefix(prefix)
        }
        if !manifest.contains(where: { $0.path == directory }) {
            manifest.append(opaqueDirectoryManifestEntry(directory))
        }
    }

    private func removePath(
        _ path: String,
        from manifest: inout [OrlixRootfsTarManifestEntry]
    ) {
        let prefix = path + "/"
        manifest.removeAll { entry in
            entry.path == path || entry.path.hasPrefix(prefix)
        }
    }

    private func applyEntry(
        _ entry: OrlixRootfsTarManifestEntry,
        to manifest: inout [OrlixRootfsTarManifestEntry]
    ) {
        let prefix = entry.path + "/"
        manifest.removeAll { existing in
            existing.path == entry.path ||
                (entry.type != .directory && existing.path.hasPrefix(prefix))
        }
        manifest.append(entry)
    }

    private func opaqueDirectoryManifestEntry(
        _ path: String
    ) -> OrlixRootfsTarManifestEntry {
        OrlixRootfsTarManifestEntry(
            path: path,
            size: 0,
            mode: 0o755,
            uid: 0,
            gid: 0,
            type: .directory,
            linkName: nil
        )
    }

    private func environmentDescriptor(
        environmentID: String,
        rootImageIdentifier: String,
        image: OrlixOCIImageLayoutImport,
        rootDirectory: URL
    ) throws -> OrlixEnvironmentDescriptor {
        var command = image.processDefaults.entrypoint + image.processDefaults.command
        if command.isEmpty {
            command = ["/bin/sh"]
        }
        let user = try userAndGroup(
            image.processDefaults.user,
            rootDirectory: rootDirectory
        )
        let workingDirectory = try validatedWorkingDirectory(
            image.processDefaults.workingDirectory
        )
        return OrlixEnvironmentDescriptor(
            id: environmentID,
            source: .ociLayout,
            platform: image.platform,
            rootImageIdentifier: rootImageIdentifier,
            defaultCommand: command,
            defaultEnvironment: image.processDefaults.environment,
            defaultWorkingDirectory: workingDirectory,
            defaultUserID: user.uid,
            defaultGroupID: user.gid
        )
    }

    private func validatedWorkingDirectory(_ value: String?) throws -> String {
        guard let value, !value.isEmpty else {
            return "/"
        }
        guard value.hasPrefix("/"),
              !value.contains("\u{0}")
        else {
            throw OrlixOCIImageLayoutError.invalidWorkingDirectory(value)
        }
        return value
    }

    private func userAndGroup(
        _ value: String?,
        rootDirectory: URL
    ) throws -> (uid: UInt32, gid: UInt32) {
        guard let value, !value.isEmpty else {
            return (0, 0)
        }
        let parts = value.split(
            separator: ":",
            maxSplits: 1,
            omittingEmptySubsequences: false
        )
        guard parts.count == 1 || parts.count == 2,
              !parts[0].isEmpty
        else {
            throw OrlixOCIImageLayoutError.unsupportedUser(value)
        }
        let account = try OrlixLinuxAccountDatabase(rootDirectory: rootDirectory)
        let uid: UInt32
        let defaultGID: UInt32
        if let numericUID = UInt32(parts[0]) {
            uid = numericUID
            defaultGID = 0
        } else if let user = account.user(named: String(parts[0])) {
            uid = user.uid
            defaultGID = user.gid
        } else {
            throw OrlixOCIImageLayoutError.unsupportedUser(value)
        }
        guard parts.count == 2 else {
            return (uid, defaultGID)
        }

        guard !parts[1].isEmpty else {
            throw OrlixOCIImageLayoutError.unsupportedUser(value)
        }
        if let numericGID = UInt32(parts[1]) {
            return (uid, numericGID)
        }
        if let group = account.group(named: String(parts[1])) {
            return (uid, group.gid)
        }
        throw OrlixOCIImageLayoutError.unsupportedUser(value)
    }
}

private struct OrlixLinuxAccountDatabase {
    struct User {
        let name: String
        let uid: UInt32
        let gid: UInt32
    }

    struct Group {
        let name: String
        let gid: UInt32
    }

    private let usersByName: [String: User]
    private let groupsByName: [String: Group]

    init(rootDirectory: URL) throws {
        self.usersByName = try Self.users(
            at: rootDirectory
                .appendingPathComponent("etc", isDirectory: true)
                .appendingPathComponent("passwd", isDirectory: false)
        )
        self.groupsByName = try Self.groups(
            at: rootDirectory
                .appendingPathComponent("etc", isDirectory: true)
                .appendingPathComponent("group", isDirectory: false)
        )
    }

    func user(named name: String) -> User? {
        usersByName[name]
    }

    func group(named name: String) -> Group? {
        groupsByName[name]
    }

    private static func users(at url: URL) throws -> [String: User] {
        guard FileManager.default.fileExists(atPath: url.path) else {
            return [:]
        }
        let contents = try String(contentsOf: url, encoding: .utf8)
        var users: [String: User] = [:]
        for line in contents.split(separator: "\n", omittingEmptySubsequences: false) {
            let fields = line.split(separator: ":", omittingEmptySubsequences: false)
            guard fields.count >= 4,
                  !fields[0].isEmpty,
                  let uid = UInt32(fields[2]),
                  let gid = UInt32(fields[3])
            else {
                continue
            }
            let name = String(fields[0])
            users[name] = User(name: name, uid: uid, gid: gid)
        }
        return users
    }

    private static func groups(at url: URL) throws -> [String: Group] {
        guard FileManager.default.fileExists(atPath: url.path) else {
            return [:]
        }
        let contents = try String(contentsOf: url, encoding: .utf8)
        var groups: [String: Group] = [:]
        for line in contents.split(separator: "\n", omittingEmptySubsequences: false) {
            let fields = line.split(separator: ":", omittingEmptySubsequences: false)
            guard fields.count >= 3,
                  !fields[0].isEmpty,
                  let gid = UInt32(fields[2])
            else {
                continue
            }
            let name = String(fields[0])
            groups[name] = Group(name: name, gid: gid)
        }
        return groups
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixOCIDigest: Equatable, Sendable {
    public let algorithm: String
    public let hex: String

    public init(_ value: String) throws {
        let parts = value.split(separator: ":", maxSplits: 1)
        guard parts.count == 2,
              parts[0] == "sha256",
              parts[1].count == 64,
              parts[1].allSatisfy({ $0.isHexDigit })
        else {
            throw OrlixOCIImageLayoutError.invalidDigest(value)
        }
        self.algorithm = String(parts[0])
        self.hex = String(parts[1]).lowercased()
    }

    public static func sha256Hex(_ data: Data) -> String {
        OrlixSHA256.hash(data).map { String(format: "%02x", $0) }.joined()
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixOCIImageLayoutError:
    Error,
    Equatable,
    Sendable
{
    case invalidDigest(String)
    case invalidLayoutVersion(String)
    case invalidManifestSchemaVersion(Int)
    case missingPlatform(String)
    case digestMismatch(expected: String, actual: String)
    case sizeMismatch(digest: String, expected: UInt64, actual: UInt64)
    case rootfsDiffIDCountMismatch(expected: Int, actual: Int)
    case rootfsDiffIDMismatch(layerIndex: Int, expected: String, actual: String)
    case unsupportedManifestMediaType(String)
    case unsupportedConfigMediaType(String)
    case unsupportedLayerMediaType(String)
    case unsupportedRootfsType(String)
    case unsupportedUser(String)
    case invalidWorkingDirectory(String)
    case invalidEnvironmentEntry(String)
    case invalidCommandEntry(String)
    case invalidWhiteout(String)
    case decompressionFailed(String)
    case decompressedLayerTooLarge(Int)
    case destinationExists(String)
}

private func verifyDiffID(
    _ diffIDs: [String],
    layerIndex: Int,
    decodedLayerData: Data
) throws {
    guard !diffIDs.isEmpty else {
        return
    }
    let actual = "sha256:\(OrlixOCIDigest.sha256Hex(decodedLayerData))"
    guard diffIDs[layerIndex] == actual else {
        throw OrlixOCIImageLayoutError.rootfsDiffIDMismatch(
            layerIndex: layerIndex,
            expected: diffIDs[layerIndex],
            actual: actual
        )
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixOCILayerDecoder: Sendable {
    public static let defaultMaxDecodedLayerBytes = 8 * 1024 * 1024 * 1024

    public init() {}

    public func decode(
        layerData: Data,
        mediaType: String,
        maxDecodedBytes: Int = defaultMaxDecodedLayerBytes
    ) throws -> Data {
        switch mediaType {
        case "application/vnd.oci.image.layer.v1.tar",
             "application/vnd.oci.image.layer.nondistributable.v1.tar",
             "application/vnd.docker.image.rootfs.diff.tar":
            return layerData
        case "application/vnd.oci.image.layer.v1.tar+gzip",
             "application/vnd.oci.image.layer.nondistributable.v1.tar+gzip",
             "application/vnd.docker.image.rootfs.diff.tar.gzip":
            return try decodeGzip(layerData, maxDecodedBytes: maxDecodedBytes)
        default:
            throw OrlixOCIImageLayoutError.unsupportedLayerMediaType(mediaType)
        }
    }

    private func decodeGzip(
        _ data: Data,
        maxDecodedBytes: Int
    ) throws -> Data {
        guard maxDecodedBytes >= 0 else {
            throw OrlixOCIImageLayoutError.decompressedLayerTooLarge(maxDecodedBytes)
        }

        var stream = z_stream()
        let initStatus = inflateInit2_(
            &stream,
            MAX_WBITS + 32,
            ZLIB_VERSION,
            Int32(MemoryLayout<z_stream>.size)
        )
        guard initStatus == Z_OK else {
            throw OrlixOCIImageLayoutError.decompressionFailed(
                "inflateInit2 failed: \(initStatus)"
            )
        }
        defer {
            inflateEnd(&stream)
        }

        var output = Data()
        var input = Array(data)
        let chunkSize = 64 * 1024
        let status = input.withUnsafeMutableBytes { inputBuffer -> Int32 in
            guard let inputBase = inputBuffer.baseAddress else {
                return Z_DATA_ERROR
            }
            stream.next_in = inputBase.assumingMemoryBound(to: Bytef.self)
            stream.avail_in = uInt(inputBuffer.count)

            var status = Z_OK
            while status != Z_STREAM_END {
                var chunk = Array(repeating: UInt8(0), count: chunkSize)
                status = chunk.withUnsafeMutableBytes { outputBuffer -> Int32 in
                    stream.next_out = outputBuffer.baseAddress!
                        .assumingMemoryBound(to: Bytef.self)
                    stream.avail_out = uInt(outputBuffer.count)
                    return inflate(&stream, Z_NO_FLUSH)
                }

                let produced = chunkSize - Int(stream.avail_out)
                if produced > 0 {
                    output.append(chunk, count: produced)
                    if output.count > maxDecodedBytes {
                        return Z_MEM_ERROR
                    }
                }

                if status == Z_STREAM_END {
                    break
                }
                if status != Z_OK {
                    return status
                }
                if produced == 0 && stream.avail_in == 0 {
                    return Z_DATA_ERROR
                }
            }
            return status
        }

        guard status == Z_STREAM_END else {
            if status == Z_MEM_ERROR {
                throw OrlixOCIImageLayoutError.decompressedLayerTooLarge(
                    maxDecodedBytes
                )
            }
            throw OrlixOCIImageLayoutError.decompressionFailed(
                "inflate failed: \(status)"
            )
        }
        return output
    }
}

private enum OrlixOCIImageLayoutBlobStore {
    static func verifiedBlob(
        _ digest: String,
        expectedSize: UInt64,
        under layoutURL: URL
    ) throws -> Data {
        let parsed = try OrlixOCIDigest(digest)
        let url = layoutURL
            .appendingPathComponent("blobs", isDirectory: true)
            .appendingPathComponent(parsed.algorithm, isDirectory: true)
            .appendingPathComponent(parsed.hex, isDirectory: false)
        let data = try Data(contentsOf: url)
        guard UInt64(data.count) == expectedSize else {
            throw OrlixOCIImageLayoutError.sizeMismatch(
                digest: digest,
                expected: expectedSize,
                actual: UInt64(data.count)
            )
        }
        let actual = OrlixOCIDigest.sha256Hex(data)
        guard parsed.algorithm == "sha256", parsed.hex == actual else {
            throw OrlixOCIImageLayoutError.digestMismatch(
                expected: digest,
                actual: "sha256:\(actual)"
            )
        }
        return data
    }
}

private struct OrlixOCIImageLayerApplication {
    var operations: [OrlixOCIImageLayerOperation] = []
}

private enum OrlixOCIImageLayerOperation {
    case applied(OrlixRootfsTarManifestEntry)
    case removed(String)
    case opaqueDirectory(String)
}

private struct OrlixOCIImageLayerApplicator {
    let fileManager: FileManager

    func apply(
        _ data: Data,
        to rootDirectory: URL
    ) throws -> OrlixOCIImageLayerApplication {
        let root = rootDirectory.standardizedFileURL
        let records = try OrlixRootfsTarManifestReader().readRecords(from: data)
        var application = OrlixOCIImageLayerApplication()
        for record in records {
            if let whiteout = try applyWhiteout(record.entry, under: root) {
                switch whiteout {
                case let .removed(path):
                    application.operations.append(.removed(path))
                case let .opaqueDirectory(path):
                    application.operations.append(.opaqueDirectory(path))
                }
                continue
            }
            try materialize(record, data: data, under: root)
            application.operations.append(.applied(record.entry))
        }
        return application
    }

    private func applyWhiteout(
        _ entry: OrlixRootfsTarManifestEntry,
        under root: URL
    ) throws -> OrlixOCIWhiteout? {
        let components = entry.path.split(separator: "/", omittingEmptySubsequences: true)
        guard let name = components.last else {
            return nil
        }
        let parent = components.dropLast().joined(separator: "/")
        if name == ".wh..wh..opq" {
            let directory = try destination(
                for: parent.isEmpty ? "." : parent,
                under: root
            )
            if fileManager.fileExists(atPath: directory.path) {
                let contents = try fileManager.contentsOfDirectory(
                    at: directory,
                    includingPropertiesForKeys: nil
                )
                for item in contents {
                    try fileManager.removeItem(at: item)
                }
            } else {
                try fileManager.createDirectory(
                    at: directory,
                    withIntermediateDirectories: true
                )
            }
            return .opaqueDirectory(parent)
        }

        guard name.hasPrefix(".wh.") else {
            return nil
        }
        let targetName = name.dropFirst(4)
        guard !targetName.isEmpty else {
            throw OrlixOCIImageLayoutError.invalidWhiteout(entry.path)
        }
        let targetPath = parent.isEmpty
            ? String(targetName)
            : parent + "/" + String(targetName)
        let target = try destination(for: targetPath, under: root)
        if itemExistsIncludingSymbolicLink(at: target) {
            try fileManager.removeItem(at: target)
        }
        return .removed(targetPath)
    }

    private func materialize(
        _ record: OrlixRootfsTarArchiveRecord,
        data: Data,
        under root: URL
    ) throws {
        let entry = record.entry
        let target = try destination(for: entry.path, under: root)
        switch entry.type {
        case .directory:
            if fileManager.fileExists(atPath: target.path) {
                return
            }
            try fileManager.createDirectory(
                at: target,
                withIntermediateDirectories: true
            )
        case .regularFile:
            try fileManager.createDirectory(
                at: target.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try? fileManager.removeItem(at: target)
            if entry.sparseExtents.isEmpty {
                try data.subdata(in: record.payloadRange).write(
                    to: target,
                    options: [.atomic]
                )
            } else {
                try writeSparseFile(
                    entry: entry,
                    archiveData: data,
                    payloadRange: record.payloadRange,
                    to: target
                )
            }
        case .symbolicLink:
            guard let linkName = entry.linkName else {
                throw OrlixRootfsTarMaterializerError.missingLinkName(entry.path)
            }
            try fileManager.createDirectory(
                at: target.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try? fileManager.removeItem(at: target)
            try fileManager.createSymbolicLink(
                atPath: target.path,
                withDestinationPath: linkName
            )
        case .hardLink:
            guard let linkName = entry.linkName else {
                throw OrlixRootfsTarMaterializerError.missingLinkName(entry.path)
            }
            let source = try destination(for: linkName, under: root)
            try fileManager.createDirectory(
                at: target.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try? fileManager.removeItem(at: target)
            try fileManager.linkItem(at: source, to: target)
        case .characterDevice, .blockDevice, .fifo:
            try fileManager.createDirectory(
                at: target.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
        }
    }

    private func itemExistsIncludingSymbolicLink(at url: URL) -> Bool {
        if fileManager.fileExists(atPath: url.path) {
            return true
        }
        return (try? fileManager.destinationOfSymbolicLink(atPath: url.path)) != nil
    }

    private func destination(
        for path: String,
        under root: URL
    ) throws -> URL {
        let destination = root
            .appendingPathComponent(path, isDirectory: false)
            .standardizedFileURL
        let rootPath = root.path.hasSuffix("/") ? root.path : root.path + "/"
        guard destination.path == root.path || destination.path.hasPrefix(rootPath)
        else {
            throw OrlixRootfsTarMaterializerError.destinationEscapesRoot(path)
        }
        return destination
    }
}

private enum OrlixOCIWhiteout {
    case removed(String)
    case opaqueDirectory(String)
}

private func writeSparseFile(
    entry: OrlixRootfsTarManifestEntry,
    archiveData: Data,
    payloadRange: Range<Int>,
    to destination: URL
) throws {
    guard let logicalSize = entry.logicalSize else {
        throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
    }
    _ = FileManager.default.createFile(atPath: destination.path, contents: nil)
    let handle = try FileHandle(forWritingTo: destination)
    defer {
        try? handle.close()
    }

    var payloadOffset = payloadRange.lowerBound
    for extent in entry.sparseExtents {
        guard extent.length <= UInt64(Int.max) else {
            throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
        }
        let extentLength = Int(extent.length)
        guard extent.offset <= logicalSize,
              extent.length <= logicalSize - extent.offset,
              extentLength <= payloadRange.upperBound - payloadOffset
        else {
            throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
        }
        try handle.seek(toOffset: extent.offset)
        try handle.write(
            contentsOf: archiveData.subdata(
                in: payloadOffset..<(payloadOffset + extentLength)
            )
        )
        payloadOffset += extentLength
    }
    guard payloadOffset == payloadRange.upperBound else {
        throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
    }
    try handle.truncate(atOffset: logicalSize)
}

private struct OrlixOCIPlatform: Codable, Equatable {
    let os: String
    let architecture: String
    let variant: String?

    init(_ value: String) throws {
        let parts = value.split(separator: "/", omittingEmptySubsequences: false)
        guard (parts.count == 2 || parts.count == 3),
              parts.allSatisfy({ !$0.isEmpty })
        else {
            throw OrlixOCIImageLayoutError.missingPlatform(value)
        }
        self.os = String(parts[0])
        self.architecture = String(parts[1])
        self.variant = parts.count == 3 ? String(parts[2]) : nil
    }
}

private struct OCIIndex: Codable {
    let manifests: [OCIDescriptor]
}

private struct OCILayout: Codable {
    let imageLayoutVersion: String
}

private struct OCIManifest: Codable {
    let schemaVersion: Int
    let config: OCIDescriptor
    let layers: [OCIDescriptor]
}

private struct OCIDescriptor: Codable {
    let mediaType: String
    let digest: String
    let size: UInt64
    let platform: OrlixOCIPlatform?
}

private struct OCIImageConfig: Codable {
    let config: OCIProcessConfig?
    let rootfs: OCIRootfs?
}

private struct OCIRootfs: Codable {
    let type: String?
    let diffIDs: [String]?

    enum CodingKeys: String, CodingKey {
        case type
        case diffIDs = "diff_ids"
    }
}

private struct OCIProcessConfig: Codable {
    let env: [String]?
    let entrypoint: [String]?
    let cmd: [String]?
    let workingDir: String?
    let user: String?

    enum CodingKeys: String, CodingKey {
        case env = "Env"
        case entrypoint = "Entrypoint"
        case cmd = "Cmd"
        case workingDir = "WorkingDir"
        case user = "User"
    }
}

private enum OrlixSHA256 {
    private static let initialHash: [UInt32] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    ]

    private static let roundConstants: [UInt32] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    ]

    static func hash(_ data: Data) -> [UInt8] {
        var bytes = Array(data)
        let bitLength = UInt64(bytes.count) * 8
        bytes.append(0x80)
        while bytes.count % 64 != 56 {
            bytes.append(0)
        }
        for shift in stride(from: 56, through: 0, by: -8) {
            bytes.append(UInt8((bitLength >> UInt64(shift)) & 0xff))
        }

        var hash = initialHash
        var schedule = Array(repeating: UInt32(0), count: 64)
        for chunkOffset in stride(from: 0, to: bytes.count, by: 64) {
            for index in 0..<16 {
                let offset = chunkOffset + index * 4
                schedule[index] =
                    UInt32(bytes[offset]) << 24 |
                    UInt32(bytes[offset + 1]) << 16 |
                    UInt32(bytes[offset + 2]) << 8 |
                    UInt32(bytes[offset + 3])
            }
            for index in 16..<64 {
                let s0 = rotateRight(schedule[index - 15], by: 7) ^
                    rotateRight(schedule[index - 15], by: 18) ^
                    (schedule[index - 15] >> 3)
                let s1 = rotateRight(schedule[index - 2], by: 17) ^
                    rotateRight(schedule[index - 2], by: 19) ^
                    (schedule[index - 2] >> 10)
                schedule[index] = schedule[index - 16]
                    &+ s0
                    &+ schedule[index - 7]
                    &+ s1
            }

            var a = hash[0]
            var b = hash[1]
            var c = hash[2]
            var d = hash[3]
            var e = hash[4]
            var f = hash[5]
            var g = hash[6]
            var h = hash[7]

            for index in 0..<64 {
                let s1 = rotateRight(e, by: 6) ^
                    rotateRight(e, by: 11) ^
                    rotateRight(e, by: 25)
                let ch = (e & f) ^ (~e & g)
                let temp1 = h
                    &+ s1
                    &+ ch
                    &+ roundConstants[index]
                    &+ schedule[index]
                let s0 = rotateRight(a, by: 2) ^
                    rotateRight(a, by: 13) ^
                    rotateRight(a, by: 22)
                let maj = (a & b) ^ (a & c) ^ (b & c)
                let temp2 = s0 &+ maj

                h = g
                g = f
                f = e
                e = d &+ temp1
                d = c
                c = b
                b = a
                a = temp1 &+ temp2
            }

            hash[0] = hash[0] &+ a
            hash[1] = hash[1] &+ b
            hash[2] = hash[2] &+ c
            hash[3] = hash[3] &+ d
            hash[4] = hash[4] &+ e
            hash[5] = hash[5] &+ f
            hash[6] = hash[6] &+ g
            hash[7] = hash[7] &+ h
        }

        return hash.flatMap { word in
            [
                UInt8((word >> 24) & 0xff),
                UInt8((word >> 16) & 0xff),
                UInt8((word >> 8) & 0xff),
                UInt8(word & 0xff)
            ]
        }
    }

    private static func rotateRight(_ value: UInt32, by bits: UInt32) -> UInt32 {
        (value >> bits) | (value << (32 - bits))
    }
}
