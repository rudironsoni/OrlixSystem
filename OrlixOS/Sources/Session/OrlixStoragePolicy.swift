import Foundation

@_spi(OrlixPrivateTesting)
public enum OrlixDocumentsMountPolicy: Equatable, Sendable {
    case explicitMountOnly
}

@_spi(OrlixPrivateTesting)
public struct OrlixStoragePolicy: Equatable, Sendable {
    public static let current = OrlixStoragePolicy(
        linuxStateDirectoryName: "Orlix",
        cacheDirectoryName: "Orlix",
        scratchDirectoryName: "Orlix",
        linuxTemporaryFilesystemType: "tmpfs",
        documentsMountPolicy: .explicitMountOnly
    )

    public let linuxStateDirectoryName: String
    public let cacheDirectoryName: String
    public let scratchDirectoryName: String
    public let linuxTemporaryFilesystemType: String
    public let documentsMountPolicy: OrlixDocumentsMountPolicy

    public func linuxStateDirectory(
        fileManager: FileManager = .default
    ) throws -> URL {
        try fileManager.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: false
        ).appendingPathComponent(linuxStateDirectoryName, isDirectory: true)
    }

    public func cacheDirectory(
        fileManager: FileManager = .default
    ) throws -> URL {
        try fileManager.url(
            for: .cachesDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: false
        ).appendingPathComponent(cacheDirectoryName, isDirectory: true)
    }

    public func scratchDirectory(
        fileManager: FileManager = .default
    ) -> URL {
        fileManager.temporaryDirectory
            .appendingPathComponent(scratchDirectoryName, isDirectory: true)
    }
}
