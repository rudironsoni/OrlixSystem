import Foundation

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentImageMaterializationPlan: Equatable, Sendable {
    public let stagingRootDirectory: URL
    public let baseTreeDirectory: URL
    public let stateTreeDirectory: URL
    public let baseMetadataCommandsURL: URL
    public let stateMetadataCommandsURL: URL
    public let baseImageURL: URL
    public let stateImageURL: URL
    public let baseImageSize: String
    public let stateImageSize: String
    public let baseLabel: String
    public let stateLabel: String
    public let rootOwner: String

    public static func plan(
        stagingRootDirectory: URL,
        storageLayout: OrlixEnvironmentStorageLayout,
        baseImageSize: String = "64m",
        stateImageSize: String = "32m"
    ) throws -> OrlixEnvironmentImageMaterializationPlan {
        try validateSize(baseImageSize)
        try validateSize(stateImageSize)
        return OrlixEnvironmentImageMaterializationPlan(
            stagingRootDirectory: stagingRootDirectory,
            baseTreeDirectory: storageLayout.importScratchDirectory
                .appendingPathComponent("base-tree", isDirectory: true),
            stateTreeDirectory: storageLayout.importScratchDirectory
                .appendingPathComponent("state-tree", isDirectory: true),
            baseMetadataCommandsURL: storageLayout.importScratchDirectory
                .appendingPathComponent(
                    "base-debugfs.commands",
                    isDirectory: false
                ),
            stateMetadataCommandsURL: storageLayout.importScratchDirectory
                .appendingPathComponent(
                    "state-debugfs.commands",
                    isDirectory: false
                ),
            baseImageURL: storageLayout.baseImageURL,
            stateImageURL: storageLayout.stateImageURL,
            baseImageSize: baseImageSize,
            stateImageSize: stateImageSize,
            baseLabel: "ORLIXROOT",
            stateLabel: "ORLIXSTATE",
            rootOwner: "0:0"
        )
    }

    public func prepareInputTrees(
        fileManager: FileManager = .default
    ) throws {
        try replaceDirectory(
            baseTreeDirectory,
            fileManager: fileManager
        )
        try copyDirectoryContents(
            from: stagingRootDirectory,
            to: baseTreeDirectory,
            fileManager: fileManager
        )
        try ensureBaseRootDirectories(fileManager: fileManager)

        try replaceDirectory(
            stateTreeDirectory,
            fileManager: fileManager
        )
        try fileManager.createDirectory(
            at: stateTreeDirectory.appendingPathComponent("upper", isDirectory: true),
            withIntermediateDirectories: true
        )
        try fileManager.createDirectory(
            at: stateTreeDirectory.appendingPathComponent("work", isDirectory: true),
            withIntermediateDirectories: true
        )
        try setPermissions(0o755, at: stateTreeDirectory, fileManager: fileManager)
        try setPermissions(
            0o755,
            at: stateTreeDirectory.appendingPathComponent("upper", isDirectory: true),
            fileManager: fileManager
        )
        try setPermissions(
            0o755,
            at: stateTreeDirectory.appendingPathComponent("work", isDirectory: true),
            fileManager: fileManager
        )
    }

    public func commands(
        mke2fsExecutable: String = "mke2fs",
        truncateExecutable: String = "truncate",
        debugfsExecutable: String = "debugfs"
    ) throws -> [OrlixEnvironmentImageMaterializationCommand] {
        try validateExecutableName(mke2fsExecutable)
        try validateExecutableName(truncateExecutable)
        try validateExecutableName(debugfsExecutable)
        return [
            truncateCommand(
                executable: truncateExecutable,
                size: baseImageSize,
                imageURL: baseImageURL
            ),
            mke2fsCommand(
                executable: mke2fsExecutable,
                sourceTree: baseTreeDirectory,
                imageURL: baseImageURL,
                label: baseLabel
            ),
            debugfsCommand(
                executable: debugfsExecutable,
                commandsURL: baseMetadataCommandsURL,
                imageURL: baseImageURL
            ),
            truncateCommand(
                executable: truncateExecutable,
                size: stateImageSize,
                imageURL: stateImageURL
            ),
            mke2fsCommand(
                executable: mke2fsExecutable,
                sourceTree: stateTreeDirectory,
                imageURL: stateImageURL,
                label: stateLabel
            ),
            debugfsCommand(
                executable: debugfsExecutable,
                commandsURL: stateMetadataCommandsURL,
                imageURL: stateImageURL
            )
        ]
    }

    public func baseImageMetadataCommands(
        manifest: [OrlixRootfsTarManifestEntry]
    ) throws -> [String] {
        try manifest.flatMap { entry -> [String] in
            if let commands = try specialFileCommands(for: entry) {
                return commands
            }
            let path = try debugfsPath(for: entry.path)
            var commands = [
                "set_inode_field \(path) uid \(entry.uid)",
                "set_inode_field \(path) gid \(entry.gid)",
                "set_inode_field \(path) mode \(debugfsMode(for: entry))",
                "set_inode_field \(path) mtime \(entry.modificationTime)"
            ]
            commands.append(
                contentsOf: try extendedAttributeCommands(for: entry, path: path)
            )
            return commands
        }
    }

    public func writeBaseImageMetadataCommands(
        manifest: [OrlixRootfsTarManifestEntry],
        fileManager: FileManager = .default
    ) throws {
        let commands = try baseImageMetadataCommands(manifest: manifest)
        try fileManager.createDirectory(
            at: baseMetadataCommandsURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try (commands.joined(separator: "\n") + "\n")
            .data(using: .utf8)!
            .write(to: baseMetadataCommandsURL, options: [.atomic])
    }

    public func stateImageMetadataCommands() -> [String] {
        [
            "set_inode_field /upper uid 0",
            "set_inode_field /upper gid 0",
            "set_inode_field /upper mode 040755",
            "set_inode_field /work uid 0",
            "set_inode_field /work gid 0",
            "set_inode_field /work mode 040755"
        ]
    }

    public func writeStateImageMetadataCommands(
        fileManager: FileManager = .default
    ) throws {
        let commands = stateImageMetadataCommands()
        try fileManager.createDirectory(
            at: stateMetadataCommandsURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try (commands.joined(separator: "\n") + "\n")
            .data(using: .utf8)!
            .write(to: stateMetadataCommandsURL, options: [.atomic])
    }

    private static func validateSize(_ size: String) throws {
        guard !size.isEmpty,
              !size.contains("/"),
              !size.contains("\\"),
              !size.contains("\u{0}")
        else {
            throw OrlixEnvironmentImageMaterializationError.invalidImageSize(size)
        }
    }

    private func ensureBaseRootDirectories(
        fileManager: FileManager
    ) throws {
        let directories: [(String, Int)] = [
            ("dev", 0o755),
            ("proc", 0o755),
            ("root", 0o700),
            ("run", 0o755),
            ("sys", 0o755),
            ("tmp", 0o1777)
        ]
        for directory in directories {
            let url = baseTreeDirectory.appendingPathComponent(
                directory.0,
                isDirectory: true
            )
            try fileManager.createDirectory(
                at: url,
                withIntermediateDirectories: true
            )
            try setPermissions(directory.1, at: url, fileManager: fileManager)
        }
        try setPermissions(0o755, at: baseTreeDirectory, fileManager: fileManager)
    }

    private func truncateCommand(
        executable: String,
        size: String,
        imageURL: URL
    ) -> OrlixEnvironmentImageMaterializationCommand {
        OrlixEnvironmentImageMaterializationCommand(
            executable: executable,
            arguments: [
                "-s",
                size,
                imageURL.path
            ]
        )
    }

    private func debugfsCommand(
        executable: String,
        commandsURL: URL,
        imageURL: URL
    ) -> OrlixEnvironmentImageMaterializationCommand {
        OrlixEnvironmentImageMaterializationCommand(
            executable: executable,
            arguments: [
                "-w",
                "-f",
                commandsURL.path,
                imageURL.path
            ]
        )
    }

    private func mke2fsCommand(
        executable: String,
        sourceTree: URL,
        imageURL: URL,
        label: String
    ) -> OrlixEnvironmentImageMaterializationCommand {
        OrlixEnvironmentImageMaterializationCommand(
            executable: executable,
            arguments: [
                "-q",
                "-t",
                "ext4",
                "-F",
                "-m",
                "0",
                "-U",
                "clear",
                "-L",
                label,
                "-E",
                "root_owner=\(rootOwner)",
                "-d",
                sourceTree.path,
                imageURL.path
            ]
        )
    }
}

@_spi(OrlixPrivateTesting)
public struct OrlixEnvironmentImageMaterializationCommand:
    Equatable,
    Sendable
{
    public let executable: String
    public let arguments: [String]
}

@_spi(OrlixPrivateTesting)
public enum OrlixEnvironmentImageMaterializationError:
    Error,
    Equatable,
    Sendable
{
    case invalidImageSize(String)
    case invalidExecutable(String)
    case invalidDebugfsPath(String)
    case missingDeviceNumber(String)
}

private func replaceDirectory(
    _ url: URL,
    fileManager: FileManager
) throws {
    if fileManager.fileExists(atPath: url.path) {
        try fileManager.removeItem(at: url)
    }
    try fileManager.createDirectory(at: url, withIntermediateDirectories: true)
}

private func copyDirectoryContents(
    from source: URL,
    to destination: URL,
    fileManager: FileManager
) throws {
    let contents = try fileManager.contentsOfDirectory(
        at: source,
        includingPropertiesForKeys: nil
    )
    for item in contents {
        try fileManager.copyItem(
            at: item,
            to: destination.appendingPathComponent(item.lastPathComponent)
        )
    }
}

private func setPermissions(
    _ permissions: Int,
    at url: URL,
    fileManager: FileManager
) throws {
    try fileManager.setAttributes(
        [.posixPermissions: NSNumber(value: permissions)],
        ofItemAtPath: url.path
    )
}

private func validateExecutableName(_ executable: String) throws {
    guard !executable.isEmpty,
          !executable.contains("\u{0}")
    else {
        throw OrlixEnvironmentImageMaterializationError.invalidExecutable(
            executable
        )
    }
}

private func debugfsPath(for path: String) throws -> String {
    guard !path.isEmpty,
          !path.utf8.contains(0),
          !path.contains("\n"),
          !path.contains("\r")
    else {
        throw OrlixEnvironmentImageMaterializationError.invalidDebugfsPath(path)
    }
    let absolutePath = "/" + path
    let escaped = absolutePath
        .replacingOccurrences(of: "\\", with: "\\\\")
        .replacingOccurrences(of: "\"", with: "\\\"")
    return "\"\(escaped)\""
}

private func debugfsMode(for entry: OrlixRootfsTarManifestEntry) -> String {
    let typeBits: UInt32
    switch entry.type {
    case .regularFile, .hardLink:
        typeBits = 0o100000
    case .directory:
        typeBits = 0o040000
    case .symbolicLink:
        typeBits = 0o120000
    case .characterDevice:
        typeBits = 0o020000
    case .blockDevice:
        typeBits = 0o060000
    case .fifo:
        typeBits = 0o010000
    }
    return "0" + String(typeBits | (entry.mode & 0o7777), radix: 8)
}

private func specialFileCommands(
    for entry: OrlixRootfsTarManifestEntry
) throws -> [String]? {
    switch entry.type {
    case .characterDevice, .blockDevice, .fifo:
        break
    case .regularFile, .directory, .symbolicLink, .hardLink:
        return nil
    }

    let path = try debugfsUnquotedPath(for: entry.path)
    let parentDirectory = try debugfsParentDirectory(for: entry.path)
    let name = try debugfsBasename(for: entry.path)
    switch entry.type {
    case .characterDevice:
        guard let major = entry.deviceMajor,
              let minor = entry.deviceMinor
        else {
            throw OrlixEnvironmentImageMaterializationError
                .missingDeviceNumber(entry.path)
        }
        var commands = [
            "cd \(parentDirectory)",
            "mknod \(name) c \(major) \(minor)",
            "cd /",
            "set_inode_field \(path) uid \(entry.uid)",
            "set_inode_field \(path) gid \(entry.gid)",
            "set_inode_field \(path) mode \(debugfsMode(for: entry))",
            "set_inode_field \(path) mtime \(entry.modificationTime)"
        ]
        commands.append(
            contentsOf: try extendedAttributeCommands(for: entry, path: path)
        )
        return commands
    case .blockDevice:
        guard let major = entry.deviceMajor,
              let minor = entry.deviceMinor
        else {
            throw OrlixEnvironmentImageMaterializationError
                .missingDeviceNumber(entry.path)
        }
        var commands = [
            "cd \(parentDirectory)",
            "mknod \(name) b \(major) \(minor)",
            "cd /",
            "set_inode_field \(path) uid \(entry.uid)",
            "set_inode_field \(path) gid \(entry.gid)",
            "set_inode_field \(path) mode \(debugfsMode(for: entry))",
            "set_inode_field \(path) mtime \(entry.modificationTime)"
        ]
        commands.append(
            contentsOf: try extendedAttributeCommands(for: entry, path: path)
        )
        return commands
    case .fifo:
        var commands = [
            "cd \(parentDirectory)",
            "mknod \(name) p",
            "cd /",
            "set_inode_field \(path) uid \(entry.uid)",
            "set_inode_field \(path) gid \(entry.gid)",
            "set_inode_field \(path) mode \(debugfsMode(for: entry))",
            "set_inode_field \(path) mtime \(entry.modificationTime)"
        ]
        commands.append(
            contentsOf: try extendedAttributeCommands(for: entry, path: path)
        )
        return commands
    case .regularFile, .directory, .symbolicLink, .hardLink:
        return nil
    }
}

private func extendedAttributeCommands(
    for entry: OrlixRootfsTarManifestEntry,
    path: String
) throws -> [String] {
    try entry.extendedAttributes.keys.sorted().map { name in
        let value = entry.extendedAttributes[name]!
        return "ea_set \(path) \(try debugfsXattrName(name)) \(try debugfsQuotedValue(value))"
    }
}

private func debugfsParentDirectory(for path: String) throws -> String {
    _ = try debugfsUnquotedPath(for: path)
    let components = path.split(separator: "/", omittingEmptySubsequences: true)
    guard !components.isEmpty else {
        throw OrlixEnvironmentImageMaterializationError.invalidDebugfsPath(path)
    }
    let parent = components.dropLast().joined(separator: "/")
    return parent.isEmpty ? "/" : "/" + parent
}

private func debugfsBasename(for path: String) throws -> String {
    _ = try debugfsUnquotedPath(for: path)
    guard let basename = path
        .split(separator: "/", omittingEmptySubsequences: true)
        .last
    else {
        throw OrlixEnvironmentImageMaterializationError.invalidDebugfsPath(path)
    }
    return String(basename)
}

private func debugfsUnquotedPath(for path: String) throws -> String {
    guard !path.isEmpty,
          !path.utf8.contains(0),
          !path.contains("\n"),
          !path.contains("\r"),
          !path.contains(" "),
          !path.contains("\t"),
          !path.contains("\\"),
          !path.contains("\"")
    else {
        throw OrlixEnvironmentImageMaterializationError.invalidDebugfsPath(path)
    }
    return "/" + path
}

private func debugfsXattrName(_ name: String) throws -> String {
    guard !name.isEmpty,
          !name.utf8.contains(0),
          !name.contains("\n"),
          !name.contains("\r"),
          !name.contains(" "),
          !name.contains("\t"),
          !name.contains("\\"),
          !name.contains("\"")
    else {
        throw OrlixEnvironmentImageMaterializationError.invalidDebugfsPath(name)
    }
    return name
}

private func debugfsQuotedValue(_ value: String) throws -> String {
    guard !value.utf8.contains(0),
          !value.contains("\n"),
          !value.contains("\r")
    else {
        throw OrlixEnvironmentImageMaterializationError.invalidDebugfsPath(value)
    }
    let escaped = value
        .replacingOccurrences(of: "\\", with: "\\\\")
        .replacingOccurrences(of: "\"", with: "\\\"")
    return "\"\(escaped)\""
}
