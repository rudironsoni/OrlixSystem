import Foundation

@_spi(OrlixPrivateTesting)
public struct OrlixRootfsTarImportPlan: Equatable, Sendable {
    public let archiveURL: URL
    public let descriptor: OrlixEnvironmentDescriptor
    public let storageLayout: OrlixEnvironmentStorageLayout

    public var stagingRootDirectory: URL {
        storageLayout.importScratchDirectory
            .appendingPathComponent("rootfs", isDirectory: true)
    }

    public static func plan(
        archiveURL: URL,
        environmentID: String,
        registry: OrlixEnvironmentRegistry,
        rootImageIdentifier: String
    ) throws -> OrlixRootfsTarImportPlan {
        let descriptor = OrlixEnvironmentDescriptor(
            id: environmentID,
            source: .rootfsTar,
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
            defaultGroupID: 0
        )
        return OrlixRootfsTarImportPlan(
            archiveURL: archiveURL,
            descriptor: descriptor,
            storageLayout: try registry.layout(forEnvironmentID: environmentID)
        )
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixRootfsTarImportResult: Equatable, Sendable {
    public let descriptor: OrlixEnvironmentDescriptor
    public let storageLayout: OrlixEnvironmentStorageLayout
    public let stagingRootDirectory: URL
    public let materializationPlan: OrlixEnvironmentImageMaterializationPlan
    public let manifest: [OrlixRootfsTarManifestEntry]
}

@_spi(OrlixPrivateTesting)
public struct OrlixRootfsTarImporter: Sendable {
    public init() {}

    public func importArchive(
        using plan: OrlixRootfsTarImportPlan,
        registry: OrlixEnvironmentRegistry,
        fileManager: FileManager = .default
    ) throws -> OrlixRootfsTarImportResult {
        try Self.validateDestinationAvailable(
            plan,
            fileManager: fileManager
        )
        let data = try Data(contentsOf: plan.archiveURL)
        return try importArchiveData(
            data,
            using: plan,
            registry: registry,
            fileManager: fileManager
        )
    }

    public func importArchiveData(
        _ data: Data,
        using plan: OrlixRootfsTarImportPlan,
        registry: OrlixEnvironmentRegistry,
        fileManager: FileManager = .default
    ) throws -> OrlixRootfsTarImportResult {
        try Self.validateDestinationAvailable(
            plan,
            fileManager: fileManager
        )
        _ = try registry.prepareStorage(
            for: plan.descriptor,
            fileManager: fileManager
        )
        var shouldRemoveEnvironmentRootOnFailure = true
        let temporaryRootDirectory = plan.storageLayout.importScratchDirectory
            .appendingPathComponent(
                ".rootfs.tar-import-\(UUID().uuidString)",
                isDirectory: true
            )
        do {
            try? fileManager.removeItem(at: temporaryRootDirectory)
            let manifest = try OrlixRootfsTarMaterializer().materialize(
                data,
                into: temporaryRootDirectory,
                fileManager: fileManager
            )
            if fileManager.fileExists(atPath: plan.stagingRootDirectory.path) {
                try fileManager.removeItem(at: plan.stagingRootDirectory)
            }
            try fileManager.moveItem(
                at: temporaryRootDirectory,
                to: plan.stagingRootDirectory
            )
            let materializationPlan = try OrlixEnvironmentImageMaterializationPlan.plan(
                stagingRootDirectory: plan.stagingRootDirectory,
                storageLayout: plan.storageLayout
            )
            try materializationPlan.prepareInputTrees(fileManager: fileManager)
            try materializationPlan.writeBaseImageMetadataCommands(
                manifest: manifest,
                fileManager: fileManager
            )
            try materializationPlan.writeStateImageMetadataCommands(
                fileManager: fileManager
            )
            try registry.save(plan.descriptor, fileManager: fileManager)
            shouldRemoveEnvironmentRootOnFailure = false
            return OrlixRootfsTarImportResult(
                descriptor: plan.descriptor,
                storageLayout: plan.storageLayout,
                stagingRootDirectory: plan.stagingRootDirectory,
                materializationPlan: materializationPlan,
                manifest: manifest
            )
        } catch {
            try? fileManager.removeItem(at: temporaryRootDirectory)
            if shouldRemoveEnvironmentRootOnFailure {
                try? fileManager.removeItem(at: plan.storageLayout.rootDirectory)
            }
            throw error
        }
    }

    private static func validateDestinationAvailable(
        _ plan: OrlixRootfsTarImportPlan,
        fileManager: FileManager
    ) throws {
        if fileManager.fileExists(atPath: plan.storageLayout.rootDirectory.path) {
            throw OrlixRootfsTarImportError.destinationExists(
                plan.descriptor.id
            )
        }
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixRootfsTarImportError:
    Error,
    Equatable,
    Sendable
{
    case destinationExists(String)
}

@_spi(OrlixPrivateTesting)
public struct OrlixRootfsTarManifestEntry: Equatable, Sendable {
    public let path: String
    public let size: UInt64
    public let mode: UInt32
    public let uid: UInt32
    public let gid: UInt32
    public let type: OrlixRootfsTarEntryType
    public let linkName: String?
    public let deviceMajor: UInt32?
    public let deviceMinor: UInt32?
    public let extendedAttributes: [String: String]
    public let sparseExtents: [OrlixRootfsTarSparseExtent]
    public let logicalSize: UInt64?

    public init(
        path: String,
        size: UInt64,
        mode: UInt32,
        uid: UInt32,
        gid: UInt32,
        type: OrlixRootfsTarEntryType,
        linkName: String?,
        deviceMajor: UInt32? = nil,
        deviceMinor: UInt32? = nil,
        extendedAttributes: [String: String] = [:],
        sparseExtents: [OrlixRootfsTarSparseExtent] = [],
        logicalSize: UInt64? = nil
    ) {
        self.path = path
        self.size = size
        self.mode = mode
        self.uid = uid
        self.gid = gid
        self.type = type
        self.linkName = linkName
        self.deviceMajor = deviceMajor
        self.deviceMinor = deviceMinor
        self.extendedAttributes = extendedAttributes
        self.sparseExtents = sparseExtents
        self.logicalSize = logicalSize
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixRootfsTarSparseExtent:
    Equatable,
    Sendable
{
    public let offset: UInt64
    public let length: UInt64

    public init(offset: UInt64, length: UInt64) {
        self.offset = offset
        self.length = length
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixRootfsTarEntryType: Equatable, Sendable {
    case regularFile
    case directory
    case symbolicLink
    case hardLink
    case characterDevice
    case blockDevice
    case fifo
}

@_spi(OrlixPrivateTesting)
public struct OrlixRootfsTarManifestReader: Sendable {
    public init() {}

    public func readManifest(from data: Data) throws -> [OrlixRootfsTarManifestEntry] {
        try readRecords(from: data).map(\.entry)
    }

    func readRecords(from data: Data) throws -> [OrlixRootfsTarArchiveRecord] {
        var records: [OrlixRootfsTarArchiveRecord] = []
        var extendedHeader = OrlixRootfsTarPAXExtendedHeader()
        var offset = 0
        while offset < data.count {
            guard offset + 512 <= data.count else {
                throw OrlixRootfsTarManifestError.truncatedHeader
            }
            let header = data.subdata(in: offset..<(offset + 512))
            if header.allSatisfy({ $0 == 0 }) {
                return records
            }

            try validateChecksum(header)
            let typeFlag = header[156]
            let headerSize = try parseNumeric(header, range: 124..<136)
            let headerPayloadRange = (offset + 512)..<(offset + 512 + Int(headerSize))
            if headerPayloadRange.upperBound > data.count {
                throw OrlixRootfsTarManifestError.truncatedPayload
            }
            if typeFlag == UInt8(ascii: "x") {
                extendedHeader = try parsePAXExtendedHeader(
                    data.subdata(in: headerPayloadRange)
                )
                offset += 512 + paddedSize(for: Int(headerSize))
                continue
            }
            if typeFlag == UInt8(ascii: "g") {
                offset += 512 + paddedSize(for: Int(headerSize))
                continue
            }
            if typeFlag == UInt8(ascii: "L") {
                extendedHeader.path = try parseGNULongName(
                    data.subdata(in: headerPayloadRange)
                )
                offset += 512 + paddedSize(for: Int(headerSize))
                continue
            }
            if typeFlag == UInt8(ascii: "K") {
                extendedHeader.linkPath = try parseGNULongName(
                    data.subdata(in: headerPayloadRange)
                )
                offset += 512 + paddedSize(for: Int(headerSize))
                continue
            }

            let entry = try parseHeader(header, extendedHeader: extendedHeader)
            extendedHeader = OrlixRootfsTarPAXExtendedHeader()
            let size: Int
            if let entry {
                size = Int(entry.size)
                records.append(
                    OrlixRootfsTarArchiveRecord(
                        entry: entry,
                        payloadRange: (offset + 512)..<(offset + 512 + Int(entry.size))
                    )
                )
            } else {
                size = Int(headerSize)
            }

            offset += 512 + paddedSize(for: size)
            if offset > data.count {
                throw OrlixRootfsTarManifestError.truncatedPayload
            }
        }
        return records
    }

    private func parseHeader(
        _ header: Data,
        extendedHeader: OrlixRootfsTarPAXExtendedHeader
    ) throws -> OrlixRootfsTarManifestEntry? {
        let rawPath: String
        if let path = extendedHeader.path {
            rawPath = path
        } else {
            let headerPath = try string(header, range: 0..<100)
            let prefix = try string(header, range: 345..<500)
            rawPath = prefix.isEmpty ? headerPath : "\(prefix)/\(headerPath)"
        }
        guard let path = try OrlixRootfsTarEntryPathPolicy
            .normalizedRelativePath(rawPath)
        else {
            return nil
        }

        let typeFlag = header[156]
        let type: OrlixRootfsTarEntryType
        switch typeFlag {
        case 0, UInt8(ascii: "0"):
            type = .regularFile
        case UInt8(ascii: "1"):
            type = .hardLink
        case UInt8(ascii: "2"):
            type = .symbolicLink
        case UInt8(ascii: "3"):
            type = .characterDevice
        case UInt8(ascii: "4"):
            type = .blockDevice
        case UInt8(ascii: "5"):
            type = .directory
        case UInt8(ascii: "6"):
            type = .fifo
        default:
            throw OrlixRootfsTarManifestError.unsupportedEntryType(typeFlag)
        }

        let sparseExtents = try extendedHeader.sparseExtents()
        let logicalSize = try extendedHeader.logicalSize(forSparseExtents: sparseExtents)
        let size: UInt64
        if let extendedSize = extendedHeader.size {
            size = extendedSize
        } else {
            size = UInt64(try parseNumeric(header, range: 124..<136))
        }
        let mode: UInt32
        if let extendedMode = extendedHeader.mode {
            mode = extendedMode
        } else {
            mode = UInt32(try parseNumeric(header, range: 100..<108))
        }
        let uid: UInt32
        if let extendedUID = extendedHeader.uid {
            uid = extendedUID
        } else {
            uid = UInt32(try parseNumeric(header, range: 108..<116))
        }
        let gid: UInt32
        if let extendedGID = extendedHeader.gid {
            gid = extendedGID
        } else {
            gid = UInt32(try parseNumeric(header, range: 116..<124))
        }

        return OrlixRootfsTarManifestEntry(
            path: path,
            size: size,
            mode: mode,
            uid: uid,
            gid: gid,
            type: type,
            linkName: try linkName(
                header,
                type: type,
                extendedHeader: extendedHeader
            ),
            deviceMajor: try deviceMajor(header, type: type),
            deviceMinor: try deviceMinor(header, type: type),
            extendedAttributes: extendedHeader.extendedAttributes,
            sparseExtents: sparseExtents,
            logicalSize: logicalSize
        )
    }

    private func linkName(
        _ header: Data,
        type: OrlixRootfsTarEntryType,
        extendedHeader: OrlixRootfsTarPAXExtendedHeader
    ) throws -> String? {
        switch type {
        case .regularFile, .directory, .characterDevice, .blockDevice, .fifo:
            return nil
        case .hardLink:
            let rawLinkName = try extendedHeader.linkPath ??
                string(header, range: 157..<257)
            return try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath(
                rawLinkName
            )
        case .symbolicLink:
            let rawLinkName = try extendedHeader.linkPath ??
                string(header, range: 157..<257)
            guard !rawLinkName.isEmpty, !rawLinkName.utf8.contains(0)
            else {
                throw OrlixRootfsTarManifestError.unsafeLinkName(rawLinkName)
            }
            return rawLinkName
        }
    }

    private func deviceMajor(
        _ header: Data,
        type: OrlixRootfsTarEntryType
    ) throws -> UInt32? {
        switch type {
        case .characterDevice, .blockDevice:
            return UInt32(try parseNumeric(header, range: 329..<337))
        case .regularFile, .directory, .symbolicLink, .hardLink, .fifo:
            return nil
        }
    }

    private func deviceMinor(
        _ header: Data,
        type: OrlixRootfsTarEntryType
    ) throws -> UInt32? {
        switch type {
        case .characterDevice, .blockDevice:
            return UInt32(try parseNumeric(header, range: 337..<345))
        case .regularFile, .directory, .symbolicLink, .hardLink, .fifo:
            return nil
        }
    }

    private func parsePAXExtendedHeader(
        _ data: Data
    ) throws -> OrlixRootfsTarPAXExtendedHeader {
        var header = OrlixRootfsTarPAXExtendedHeader()
        let bytes = Array(data)
        var offset = 0

        while offset < bytes.count {
            let lengthStart = offset
            while offset < bytes.count,
                  bytes[offset] >= UInt8(ascii: "0"),
                  bytes[offset] <= UInt8(ascii: "9") {
                offset += 1
            }
            guard offset > lengthStart,
                  offset < bytes.count,
                  bytes[offset] == UInt8(ascii: " ")
            else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            guard let length = Int(
                String(
                    decoding: bytes[lengthStart..<offset],
                    as: UTF8.self
                )
            ),
                length > 0,
                lengthStart + length <= bytes.count
            else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            let recordBytes = bytes[lengthStart..<(lengthStart + length)]
            guard let record = String(
                data: Data(recordBytes),
                encoding: .utf8
            ),
                record.hasSuffix("\n"),
                let separator = record.firstIndex(of: " "),
                let equals = record[record.index(after: separator)...]
                    .firstIndex(of: "=")
            else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }

            let key = String(record[record.index(after: separator)..<equals])
            let valueStart = record.index(after: equals)
            let valueEnd = record.index(before: record.endIndex)
            let value = String(record[valueStart..<valueEnd])
            try header.apply(key: key, value: value)
            offset = lengthStart + length
        }

        return header
    }

    private func parseGNULongName(_ data: Data) throws -> String {
        let bytes = Array(data)
        let end = bytes.firstIndex(of: 0) ?? bytes.count
        guard let value = String(data: Data(bytes[0..<end]), encoding: .utf8),
              !value.isEmpty
        else {
            throw OrlixRootfsTarManifestError.invalidGNULongName
        }
        return value
    }

    private func string(_ data: Data, range: Range<Int>) throws -> String {
        guard range.lowerBound >= 0, range.upperBound <= data.count else {
            throw OrlixRootfsTarManifestError.truncatedHeader
        }
        let bytes = data[range]
        let end = bytes.firstIndex(of: 0) ?? range.upperBound
        let stringData = data.subdata(in: range.lowerBound..<end)
        guard let value = String(data: stringData, encoding: .utf8) else {
            throw OrlixRootfsTarManifestError.invalidString
        }
        return value
    }

    private func parseOctal(_ data: Data, range: Range<Int>) throws -> UInt64 {
        let value = try string(data, range: range).trimmingCharacters(
            in: CharacterSet(charactersIn: " \0")
        )
        guard !value.isEmpty else {
            return 0
        }

        var result: UInt64 = 0
        for character in value.utf8 {
            guard character >= UInt8(ascii: "0"),
                  character <= UInt8(ascii: "7")
            else {
                throw OrlixRootfsTarManifestError.invalidOctal(value)
            }
            result = result * 8 + UInt64(character - UInt8(ascii: "0"))
        }
        return result
    }

    private func parseNumeric(_ data: Data, range: Range<Int>) throws -> UInt64 {
        guard range.lowerBound >= 0, range.upperBound <= data.count else {
            throw OrlixRootfsTarManifestError.truncatedHeader
        }
        if data[range.lowerBound] & 0x80 == 0 {
            return try parseOctal(data, range: range)
        }

        var result: UInt64 = 0
        for index in range {
            let byte: UInt8
            if index == range.lowerBound {
                byte = data[index] & 0x7f
            } else {
                byte = data[index]
            }
            guard result <= (UInt64.max - UInt64(byte)) / 256 else {
                throw OrlixRootfsTarManifestError.invalidNumericField
            }
            result = result * 256 + UInt64(byte)
        }
        return result
    }

    private func validateChecksum(_ header: Data) throws {
        let expected = try parseOctal(header, range: 148..<156)
        var actual: UInt64 = 0
        for index in 0..<512 {
            if index >= 148 && index < 156 {
                actual += UInt64(UInt8(ascii: " "))
            } else {
                actual += UInt64(header[index])
            }
        }
        if expected != actual {
            throw OrlixRootfsTarManifestError.invalidChecksum(
                expected: expected,
                actual: actual
            )
        }
    }

    private func paddedSize(for size: Int) -> Int {
        ((size + 511) / 512) * 512
    }
}

struct OrlixRootfsTarArchiveRecord {
    let entry: OrlixRootfsTarManifestEntry
    let payloadRange: Range<Int>
}

private struct OrlixRootfsTarPAXExtendedHeader {
    var path: String?
    var linkPath: String?
    var size: UInt64?
    var mode: UInt32?
    var uid: UInt32?
    var gid: UInt32?
    var extendedAttributes: [String: String] = [:]
    var gnuSparseMap: String?
    var gnuSparseSize: UInt64?
    var gnuSparseRealSize: UInt64?

    mutating func apply(key: String, value: String) throws {
        switch key {
        case "path":
            path = value
        case "linkpath":
            linkPath = value
        case "size":
            guard let parsed = UInt64(value) else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            size = parsed
        case "uid":
            guard let parsed = UInt32(value) else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            uid = parsed
        case "gid":
            guard let parsed = UInt32(value) else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            gid = parsed
        case "mode":
            guard let parsed = UInt32(value, radix: 8) else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            mode = parsed
        case "GNU.sparse.map":
            gnuSparseMap = value
        case "GNU.sparse.size":
            guard let parsed = UInt64(value) else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            gnuSparseSize = parsed
        case "GNU.sparse.realsize":
            guard let parsed = UInt64(value) else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            gnuSparseRealSize = parsed
        default:
            if key.hasPrefix("SCHILY.xattr.") {
                let name = String(key.dropFirst("SCHILY.xattr.".count))
                guard !name.isEmpty else {
                    throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
                }
                extendedAttributes[name] = value
            } else if key.hasPrefix("LIBARCHIVE.xattr.") {
                let encodedName = String(
                    key.dropFirst("LIBARCHIVE.xattr.".count)
                )
                let name = try urlDecode(encodedName)
                guard !name.isEmpty,
                      let decodedData = base64Decode(value),
                      let decodedValue = String(data: decodedData, encoding: .utf8)
                else {
                    throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
                }
                extendedAttributes[name] = decodedValue
            }
        }
    }

    func sparseExtents() throws -> [OrlixRootfsTarSparseExtent] {
        guard let gnuSparseMap else {
            return []
        }
        let parts = gnuSparseMap.split(separator: ",", omittingEmptySubsequences: false)
        guard parts.count.isMultiple(of: 2) else {
            throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
        }
        var extents: [OrlixRootfsTarSparseExtent] = []
        var index = 0
        while index < parts.count {
            guard let offset = UInt64(parts[index]),
                  let length = UInt64(parts[index + 1])
            else {
                throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
            }
            extents.append(
                OrlixRootfsTarSparseExtent(offset: offset, length: length)
            )
            index += 2
        }
        return extents
    }

    func logicalSize(
        forSparseExtents sparseExtents: [OrlixRootfsTarSparseExtent]
    ) throws -> UInt64? {
        guard !sparseExtents.isEmpty else {
            return nil
        }
        if let gnuSparseRealSize {
            return gnuSparseRealSize
        }
        guard let gnuSparseSize else {
            throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
        }
        return gnuSparseSize
    }

    private func urlDecode(_ value: String) throws -> String {
        var bytes: [UInt8] = []
        let scalars = Array(value.unicodeScalars)
        var index = 0
        while index < scalars.count {
            let scalar = scalars[index]
            if scalar.value == UInt8(ascii: "%") {
                guard index + 2 < scalars.count,
                      let high = hexValue(scalars[index + 1]),
                      let low = hexValue(scalars[index + 2])
                else {
                    throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
                }
                bytes.append(high << 4 | low)
                index += 3
            } else {
                guard scalar.value <= 0x7f else {
                    throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
                }
                bytes.append(UInt8(scalar.value))
                index += 1
            }
        }
        guard let decoded = String(data: Data(bytes), encoding: .utf8) else {
            throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
        }
        return decoded
    }

    private func hexValue(_ scalar: Unicode.Scalar) -> UInt8? {
        switch scalar.value {
        case UInt32(UInt8(ascii: "0"))...UInt32(UInt8(ascii: "9")):
            return UInt8(scalar.value - UInt32(UInt8(ascii: "0")))
        case UInt32(UInt8(ascii: "a"))...UInt32(UInt8(ascii: "f")):
            return UInt8(scalar.value - UInt32(UInt8(ascii: "a")) + 10)
        case UInt32(UInt8(ascii: "A"))...UInt32(UInt8(ascii: "F")):
            return UInt8(scalar.value - UInt32(UInt8(ascii: "A")) + 10)
        default:
            return nil
        }
    }

    private func base64Decode(_ value: String) -> Data? {
        let paddingLength = (4 - value.count % 4) % 4
        let padded = value + String(repeating: "=", count: paddingLength)
        return Data(base64Encoded: padded)
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixRootfsTarMaterializer: Sendable {
    public init() {}

    @discardableResult
    public func materialize(
        _ data: Data,
        into rootDirectory: URL,
        fileManager: FileManager = .default
    ) throws -> [OrlixRootfsTarManifestEntry] {
        let root = rootDirectory.standardizedFileURL
        let parent = root.deletingLastPathComponent()
        let temporaryRoot = parent.appendingPathComponent(
            ".\(root.lastPathComponent).import-\(UUID().uuidString)",
            isDirectory: true
        )
        try fileManager.createDirectory(
            at: parent,
            withIntermediateDirectories: true
        )
        try? fileManager.removeItem(at: temporaryRoot)

        do {
            let entries = try materializeValidated(
                data,
                into: temporaryRoot,
                fileManager: fileManager
            )
            if fileManager.fileExists(atPath: root.path) {
                try fileManager.removeItem(at: root)
            }
            try fileManager.moveItem(at: temporaryRoot, to: root)
            return entries
        } catch {
            try? fileManager.removeItem(at: temporaryRoot)
            throw error
        }
    }

    private func materializeValidated(
        _ data: Data,
        into rootDirectory: URL,
        fileManager: FileManager
    ) throws -> [OrlixRootfsTarManifestEntry] {
        let root = rootDirectory.standardizedFileURL
        try fileManager.createDirectory(
            at: root,
            withIntermediateDirectories: true
        )

        let records = try OrlixRootfsTarManifestReader().readRecords(from: data)
        for record in records {
            let destination = try safeDestination(
                for: record.entry.path,
                under: root
            )
            switch record.entry.type {
            case .directory:
                try fileManager.createDirectory(
                    at: destination,
                    withIntermediateDirectories: true
                )
            case .regularFile:
                try fileManager.createDirectory(
                    at: destination.deletingLastPathComponent(),
                    withIntermediateDirectories: true
                )
                if record.entry.sparseExtents.isEmpty {
                    try data.subdata(in: record.payloadRange).write(
                        to: destination,
                        options: [.atomic]
                    )
                } else {
                    try writeSparseFile(
                        entry: record.entry,
                        archiveData: data,
                        payloadRange: record.payloadRange,
                        to: destination,
                        fileManager: fileManager
                    )
                }
            case .symbolicLink:
                guard let linkName = record.entry.linkName else {
                    throw OrlixRootfsTarMaterializerError.missingLinkName(
                        record.entry.path
                    )
                }
                try fileManager.createDirectory(
                    at: destination.deletingLastPathComponent(),
                    withIntermediateDirectories: true
                )
                try? fileManager.removeItem(at: destination)
                try fileManager.createSymbolicLink(
                    atPath: destination.path,
                    withDestinationPath: linkName
                )
            case .hardLink:
                guard let linkName = record.entry.linkName else {
                    throw OrlixRootfsTarMaterializerError.missingLinkName(
                        record.entry.path
                    )
                }
                let source = try safeDestination(for: linkName, under: root)
                try fileManager.createDirectory(
                    at: destination.deletingLastPathComponent(),
                    withIntermediateDirectories: true
                )
                try? fileManager.removeItem(at: destination)
                try fileManager.linkItem(at: source, to: destination)
            case .characterDevice, .blockDevice, .fifo:
                try fileManager.createDirectory(
                    at: destination.deletingLastPathComponent(),
                    withIntermediateDirectories: true
                )
            }
        }
        return records.map(\.entry)
    }

    private func safeDestination(
        for path: String,
        under root: URL
    ) throws -> URL {
        let destination = root
            .appendingPathComponent(path, isDirectory: false)
            .standardizedFileURL
        let rootPath = root.path.hasSuffix("/") ? root.path : root.path + "/"
        guard destination.path.hasPrefix(rootPath) else {
            throw OrlixRootfsTarMaterializerError.destinationEscapesRoot(path)
        }
        return destination
    }
}

private func writeSparseFile(
    entry: OrlixRootfsTarManifestEntry,
    archiveData: Data,
    payloadRange: Range<Int>,
    to destination: URL,
    fileManager: FileManager
) throws {
    guard let logicalSize = entry.logicalSize else {
        throw OrlixRootfsTarManifestError.invalidPAXExtendedHeader
    }
    try? fileManager.removeItem(at: destination)
    _ = fileManager.createFile(atPath: destination.path, contents: nil)
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

@_spi(OrlixPrivateTesting)
public enum OrlixRootfsTarEntryPathPolicy {
    public static func normalizedRelativePath(
        _ rawPath: String
    ) throws -> String? {
        guard !rawPath.isEmpty,
              !rawPath.utf8.contains(0),
              !rawPath.hasPrefix("/")
        else {
            throw OrlixRootfsTarEntryPathError.unsafePath(rawPath)
        }

        var components = rawPath.split(separator: "/", omittingEmptySubsequences: true)
        while components.first == "." {
            components.removeFirst()
        }
        while components.last == "." {
            components.removeLast()
        }
        if components.isEmpty {
            return nil
        }
        for component in components {
            if component == "." || component == ".." {
                throw OrlixRootfsTarEntryPathError.unsafePath(rawPath)
            }
        }
        return components.joined(separator: "/")
    }
}

@_spi(OrlixPrivateTesting)
public enum OrlixRootfsTarEntryPathError:
    Error,
    Equatable,
    Sendable
{
    case unsafePath(String)
}

@_spi(OrlixPrivateTesting)
public enum OrlixRootfsTarManifestError:
    Error,
    Equatable,
    Sendable
{
    case truncatedHeader
    case truncatedPayload
    case invalidString
    case invalidOctal(String)
    case invalidChecksum(expected: UInt64, actual: UInt64)
    case unsupportedEntryType(UInt8)
    case unsafeLinkName(String)
    case invalidPAXExtendedHeader
    case invalidGNULongName
    case invalidNumericField
}

@_spi(OrlixPrivateTesting)
public enum OrlixRootfsTarMaterializerError:
    Error,
    Equatable,
    Sendable
{
    case destinationEscapesRoot(String)
    case missingLinkName(String)
}
