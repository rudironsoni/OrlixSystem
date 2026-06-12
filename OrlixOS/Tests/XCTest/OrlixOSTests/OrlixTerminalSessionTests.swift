import XCTest
import zlib
@_spi(OrlixPrivateTesting) @testable import OrlixOS

final class OrlixTerminalSessionTests: XCTestCase {
    func testBootStatusMapsAlreadyStartedResult() {
        XCTAssertEqual(OrlixBootStatus(rawStatus: -3), .alreadyStarted)
        XCTAssertEqual(
            OrlixBootStatus(rawStatus: -3).message,
            "Orlix boot already started in this process."
        )
    }

    func testDefaultEnvironmentDescriptorUsesLinuxShapedDefaults() {
        let descriptor = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.bundle.rootfs"
        )

        XCTAssertEqual(descriptor.id, "default")
        XCTAssertEqual(descriptor.source, .defaultRoot)
        XCTAssertEqual(descriptor.platform, "linux/arm64")
        XCTAssertEqual(descriptor.rootImageIdentifier, "orlix.bundle.rootfs")
        XCTAssertEqual(descriptor.defaultCommand, ["/bin/sh"])
        XCTAssertEqual(descriptor.defaultEnvironment["HOME"], "/root")
        XCTAssertEqual(
            descriptor.defaultEnvironment["PATH"],
            "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
        )
        XCTAssertEqual(descriptor.defaultWorkingDirectory, "/")
        XCTAssertEqual(descriptor.defaultUserID, 0)
        XCTAssertEqual(descriptor.defaultGroupID, 0)
        XCTAssertEqual(descriptor.rootMount, .defaultOverlay)
        XCTAssertEqual(descriptor.mounts, [])
        XCTAssertEqual(descriptor.rootMount.baseDevicePath, "/dev/vda")
        XCTAssertEqual(descriptor.rootMount.stateDevicePath, "/dev/vdb")
        XCTAssertEqual(descriptor.rootMount.lowerMountPath, "/lower")
        XCTAssertEqual(descriptor.rootMount.stateMountPath, "/state")
        XCTAssertEqual(descriptor.rootMount.overlayMountPath, "/newroot")
        XCTAssertEqual(descriptor.rootMount.upperDirectoryPath, "/state/upper")
        XCTAssertEqual(descriptor.rootMount.workDirectoryPath, "/state/work")
        XCTAssertEqual(descriptor.rootMount.filesystemType, "ext4")
        XCTAssertEqual(descriptor.rootMount.overlayFilesystemType, "overlay")
        XCTAssertTrue(descriptor.rootMount.baseReadOnly)
    }

    func testDocumentsMountIsExplicitEnvironmentDescriptorMetadata() throws {
        let documentsMount = try OrlixEnvironmentMount.documents(
            targetPath: "/home/root/Documents"
        )
        let descriptor = OrlixEnvironmentDescriptor(
            id: "documents-explicit",
            source: .copiedEnvironment(parentID: "default"),
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.documents-explicit",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0,
            mounts: [documentsMount]
        )

        XCTAssertEqual(descriptor.mounts, [documentsMount])
        XCTAssertEqual(descriptor.mounts.first?.source, .documents)
        XCTAssertEqual(descriptor.mounts.first?.targetPath, "/home/root/Documents")
        XCTAssertFalse(descriptor.mounts.first?.readOnly ?? true)

        let encoded = try JSONEncoder().encode(descriptor)
        let decoded = try JSONDecoder().decode(
            OrlixEnvironmentDescriptor.self,
            from: encoded
        )
        XCTAssertEqual(decoded, descriptor)
    }

    func testSecurityScopedExternalMountIsExplicitEnvironmentDescriptorMetadata()
        throws
    {
        let externalMount = try OrlixEnvironmentMount.securityScopedExternal(
            bookmarkID: "project-folder-bookmark",
            targetPath: "/mnt/external/project",
            readOnly: true
        )
        let descriptor = OrlixEnvironmentDescriptor(
            id: "external-explicit",
            source: .copiedEnvironment(parentID: "default"),
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.external-explicit",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0,
            mounts: [externalMount]
        )

        XCTAssertEqual(descriptor.mounts, [externalMount])
        XCTAssertEqual(
            descriptor.mounts.first?.source,
            .securityScopedExternal(bookmarkID: "project-folder-bookmark")
        )
        XCTAssertEqual(
            descriptor.mounts.first?.targetPath,
            "/mnt/external/project"
        )
        XCTAssertTrue(descriptor.mounts.first?.readOnly ?? false)

        let encoded = try JSONEncoder().encode(descriptor)
        let decoded = try JSONDecoder().decode(
            OrlixEnvironmentDescriptor.self,
            from: encoded
        )
        XCTAssertEqual(decoded, descriptor)
    }

    func testDocumentsMountRejectsReservedLinuxRuntimeTargets() throws {
        for target in ["/", "/proc", "/proc/self", "/dev", "/sys", "/run", "/tmp"] {
            XCTAssertThrowsError(
                try OrlixEnvironmentMount.documents(targetPath: target)
            ) { error in
                XCTAssertEqual(
                    error as? OrlixEnvironmentMountError,
                    .reservedTargetPath(target)
                )
            }
        }
    }

    func testSecurityScopedExternalMountRejectsUnsafeSourceIdentifiers()
        throws
    {
        for bookmarkID in ["", ".", "..", ".hidden", "a..b", "a/b", #"a\b"#, "a\u{0}b"] {
            XCTAssertThrowsError(
                try OrlixEnvironmentMount.securityScopedExternal(
                    bookmarkID: bookmarkID,
                    targetPath: "/mnt/external/project"
                )
            ) { error in
                XCTAssertEqual(
                    error as? OrlixEnvironmentMountError,
                    .invalidSourceIdentifier(bookmarkID)
                )
            }
        }
    }

    func testSecurityScopedExternalMountRejectsReservedLinuxRuntimeTargets()
        throws
    {
        for target in ["/", "/proc", "/proc/self", "/dev", "/sys", "/run", "/tmp"] {
            XCTAssertThrowsError(
                try OrlixEnvironmentMount.securityScopedExternal(
                    bookmarkID: "project-folder-bookmark",
                    targetPath: target
                )
            ) { error in
                XCTAssertEqual(
                    error as? OrlixEnvironmentMountError,
                    .reservedTargetPath(target)
                )
            }
        }
    }

    func testEnvironmentDescriptorRejectsPersistedInvalidDocumentsMountTargets()
        throws
    {
        for target in ["Documents", "/tmp", "/proc/self", "/home//root"] {
            let metadata = Data(
                """
                {
                  "defaultCommand" : [
                    "/bin/sh"
                  ],
                  "defaultEnvironment" : {
                    "PATH" : "/usr/bin:/bin"
                  },
                  "defaultGroupID" : 0,
                  "defaultUserID" : 0,
                  "defaultWorkingDirectory" : "/",
                  "id" : "bad-documents-mount",
                  "mounts" : [
                    {
                      "readOnly" : false,
                      "source" : {
                        "documents" : {}
                      },
                      "targetPath" : "\(target)"
                    }
                  ],
                  "platform" : "linux/arm64",
                  "rootImageIdentifier" : "orlix.env.bad-documents-mount",
                  "source" : {
                    "copiedEnvironment" : {
                      "parentID" : "default"
                    }
                  }
                }
                """.utf8
            )

            XCTAssertThrowsError(
                try JSONDecoder().decode(
                    OrlixEnvironmentDescriptor.self,
                    from: metadata
                )
            ) { error in
                switch target {
                case "/tmp", "/proc/self":
                    XCTAssertEqual(
                        error as? OrlixEnvironmentMountError,
                        .reservedTargetPath(target)
                    )
                default:
                    XCTAssertEqual(
                        error as? OrlixEnvironmentMountError,
                        .invalidTargetPath(target)
                    )
                }
            }
        }
    }

    func testEnvironmentStorageLayoutSeparatesStateScratchAndDownloads() throws {
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "alpine-dev"
        )

        XCTAssertTrue(
            layout.rootDirectory.path.hasSuffix(
                "Application Support/Orlix/environments/alpine-dev"
            )
        )
        XCTAssertTrue(
            layout.baseImageURL.path.hasSuffix(
                "Application Support/Orlix/environments/alpine-dev/base.ext4"
            )
        )
        XCTAssertTrue(
            layout.stateImageURL.path.hasSuffix(
                "Application Support/Orlix/environments/alpine-dev/state.ext4"
            )
        )
        XCTAssertTrue(
            layout.importScratchDirectory.path.hasSuffix(
                "Orlix/imports/alpine-dev"
            )
        )
        XCTAssertTrue(
            layout.downloadCacheDirectory.path.hasSuffix(
                "Caches/Orlix/downloads"
            )
        )
        XCTAssertFalse(layout.importScratchDirectory.path.contains("Application Support"))
        XCTAssertFalse(layout.downloadCacheDirectory.path.contains("Application Support"))
    }

    func testEnvironmentStorageLayoutRejectsUnsafeEnvironmentIDs() {
        XCTAssertThrowsError(
            try OrlixEnvironmentStorageLayout.layout(forEnvironmentID: "")
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentStorageLayout.layout(forEnvironmentID: "..")
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentStorageLayout.layout(forEnvironmentID: ".hidden")
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentStorageLayout.layout(forEnvironmentID: "a..b")
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentStorageLayout.layout(forEnvironmentID: "a/b")
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentStorageLayout.layout(forEnvironmentID: #"a\b"#)
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentStorageLayout.layout(forEnvironmentID: "a\u{0}b")
        )
    }

    func testEnvironmentRegistryPersistsDescriptorsUnderStateRoot() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let documentsMount = try OrlixEnvironmentMount.documents(
            targetPath: "/home/root/Documents"
        )
        let descriptor = OrlixEnvironmentDescriptor(
            id: "alpine-dev",
            source: .copiedEnvironment(parentID: "default"),
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.alpine-dev",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0,
            mounts: [documentsMount]
        )

        try registry.save(descriptor)

        let loaded = try registry.load(environmentID: "alpine-dev")
        let listed = try registry.list()
        let layout = try registry.layout(forEnvironmentID: "alpine-dev")

        XCTAssertEqual(loaded, descriptor)
        XCTAssertEqual(listed, [descriptor])
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: layout.rootDirectory.path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: layout.importScratchDirectory.path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: layout.downloadCacheDirectory.path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: try registry.descriptorURL(
                    forEnvironmentID: "alpine-dev"
                ).path
            )
        )
        let metadata = try Data(
            contentsOf: try registry.descriptorURL(forEnvironmentID: "alpine-dev")
        )
        let decodedMetadata = try JSONSerialization.jsonObject(
            with: metadata
        ) as? [String: Any]
        let rootMount = try XCTUnwrap(decodedMetadata?["rootMount"] as? [String: Any])
        XCTAssertEqual(rootMount["baseDevicePath"] as? String, "/dev/vda")
        XCTAssertEqual(rootMount["stateDevicePath"] as? String, "/dev/vdb")
        XCTAssertEqual(rootMount["overlayMountPath"] as? String, "/newroot")
        let mounts = try XCTUnwrap(decodedMetadata?["mounts"] as? [[String: Any]])
        XCTAssertEqual(mounts.count, 1)
        XCTAssertEqual(mounts.first?["targetPath"] as? String, "/home/root/Documents")
        XCTAssertEqual(mounts.first?["readOnly"] as? Bool, false)
    }

    func testEnvironmentDescriptorDecodesLegacyMetadataWithDefaultOverlayRootMount()
        throws
    {
        let legacyMetadata = Data(
            """
            {
              "defaultCommand" : [
                "/bin/sh"
              ],
              "defaultEnvironment" : {
                "PATH" : "/usr/bin:/bin"
              },
              "defaultGroupID" : 0,
              "defaultUserID" : 0,
              "defaultWorkingDirectory" : "/",
              "id" : "legacy-alpine",
              "platform" : "linux/arm64",
              "rootImageIdentifier" : "orlix.env.legacy-alpine",
              "source" : {
                "rootfsTar" : {}
              }
            }
            """.utf8
        )

        let descriptor = try JSONDecoder().decode(
            OrlixEnvironmentDescriptor.self,
            from: legacyMetadata
        )

        XCTAssertEqual(descriptor.id, "legacy-alpine")
        XCTAssertEqual(descriptor.source, .rootfsTar)
        XCTAssertEqual(descriptor.rootMount, .defaultOverlay)
        XCTAssertEqual(descriptor.mounts, [])
    }

    func testEnvironmentRegistryDoesNotCreateBlockImagesWhenSavingMetadata() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let descriptor = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.bundle.rootfs"
        )
        try registry.save(descriptor)

        let layout = try registry.layout(forEnvironmentID: descriptor.id)
        XCTAssertFalse(
            FileManager.default.fileExists(atPath: layout.baseImageURL.path)
        )
        XCTAssertFalse(
            FileManager.default.fileExists(atPath: layout.stateImageURL.path)
        )
    }

    func testEnvironmentRegistryCopiesNamedEnvironmentImagesAndMetadata()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let documentsMount = try OrlixEnvironmentMount.documents(
            targetPath: "/home/root/Documents",
            readOnly: true
        )
        let parent = OrlixEnvironmentDescriptor(
            id: "default",
            source: .defaultRoot,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.default",
            defaultCommand: ["/bin/sh", "-l"],
            defaultEnvironment: ["HOME": "/root", "PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/root",
            defaultUserID: 0,
            defaultGroupID: 0,
            mounts: [documentsMount]
        )
        try registry.save(parent)
        let parentLayout = try registry.layout(forEnvironmentID: parent.id)
        try Data("parent-base-image".utf8).write(to: parentLayout.baseImageURL)
        try Data("parent-state-image".utf8).write(to: parentLayout.stateImageURL)

        let copied = try registry.copyEnvironment(
            from: "default",
            to: "default-copy",
            rootImageIdentifier: "orlix.env.default-copy"
        )
        let copiedLayout = try registry.layout(forEnvironmentID: "default-copy")
        let loaded = try registry.load(environmentID: "default-copy")

        XCTAssertEqual(copied, loaded)
        XCTAssertEqual(copied.id, "default-copy")
        XCTAssertEqual(copied.source, .copiedEnvironment(parentID: "default"))
        XCTAssertEqual(copied.platform, parent.platform)
        XCTAssertEqual(copied.rootImageIdentifier, "orlix.env.default-copy")
        XCTAssertEqual(copied.defaultCommand, parent.defaultCommand)
        XCTAssertEqual(copied.defaultEnvironment, parent.defaultEnvironment)
        XCTAssertEqual(copied.defaultWorkingDirectory, parent.defaultWorkingDirectory)
        XCTAssertEqual(copied.defaultUserID, parent.defaultUserID)
        XCTAssertEqual(copied.defaultGroupID, parent.defaultGroupID)
        XCTAssertEqual(copied.rootMount, parent.rootMount)
        XCTAssertEqual(copied.mounts, parent.mounts)
        XCTAssertEqual(
            try Data(contentsOf: copiedLayout.baseImageURL),
            try Data(contentsOf: parentLayout.baseImageURL)
        )
        XCTAssertEqual(
            try Data(contentsOf: copiedLayout.stateImageURL),
            try Data(contentsOf: parentLayout.stateImageURL)
        )
        XCTAssertTrue(
            copiedLayout.baseImageURL.path.hasSuffix(
                "Application Support/Orlix/environments/default-copy/base.ext4"
            )
        )
        XCTAssertTrue(
            copiedLayout.importScratchDirectory.path.hasSuffix(
                "tmp/Orlix/imports/default-copy"
            )
        )
    }

    func testEnvironmentRegistryCopyRequiresMaterializedParentImages() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let parent = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.default"
        )
        try registry.save(parent)
        let parentLayout = try registry.layout(forEnvironmentID: parent.id)

        XCTAssertThrowsError(
            try registry.copyEnvironment(
                from: parent.id,
                to: "default-copy",
                rootImageIdentifier: "orlix.env.default-copy"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentCopyError,
                .missingParentImage(parentLayout.baseImageURL.path)
            )
        }

        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: try registry.layout(
                    forEnvironmentID: "default-copy"
                ).rootDirectory.path
            )
        )
    }

    func testEnvironmentRegistryCopyDoesNotOverwriteExistingEnvironment()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let parent = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.default"
        )
        try registry.save(parent)
        let parentLayout = try registry.layout(forEnvironmentID: parent.id)
        try Data("base".utf8).write(to: parentLayout.baseImageURL)
        try Data("state".utf8).write(to: parentLayout.stateImageURL)
        let existing = OrlixEnvironmentDescriptor(
            id: "default-copy",
            source: .copiedEnvironment(parentID: parent.id),
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.existing",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: [:],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        try registry.save(existing)

        XCTAssertThrowsError(
            try registry.copyEnvironment(
                from: parent.id,
                to: existing.id,
                rootImageIdentifier: "orlix.env.default-copy"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentCopyError,
                .destinationExists(existing.id)
            )
        }

        XCTAssertEqual(
            try registry.load(environmentID: existing.id).rootImageIdentifier,
            "orlix.env.existing"
        )
    }

    func testEnvironmentRegistryCopyCreatesIndependentImageFiles() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let parent = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.default"
        )
        try registry.save(parent)
        let parentLayout = try registry.layout(forEnvironmentID: parent.id)
        try Data("parent-base-image".utf8).write(to: parentLayout.baseImageURL)
        try Data("parent-state-image".utf8).write(to: parentLayout.stateImageURL)

        _ = try registry.copyEnvironment(
            from: parent.id,
            to: "default-copy",
            rootImageIdentifier: "orlix.env.default-copy"
        )
        let copiedLayout = try registry.layout(forEnvironmentID: "default-copy")

        try Data("copied-base-image".utf8).write(to: copiedLayout.baseImageURL)
        try Data("copied-state-image".utf8).write(to: copiedLayout.stateImageURL)

        XCTAssertEqual(
            try Data(contentsOf: parentLayout.baseImageURL),
            Data("parent-base-image".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: parentLayout.stateImageURL),
            Data("parent-state-image".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: copiedLayout.baseImageURL),
            Data("copied-base-image".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: copiedLayout.stateImageURL),
            Data("copied-state-image".utf8)
        )
    }

    func testEnvironmentRegistryCopyPreservesStateAfterParentImageChanges()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let parent = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.default"
        )
        try registry.save(parent)
        let parentLayout = try registry.layout(forEnvironmentID: parent.id)
        try Data("parent-base-image-before-copy".utf8)
            .write(to: parentLayout.baseImageURL)
        try Data("parent-state-image-before-copy".utf8)
            .write(to: parentLayout.stateImageURL)

        _ = try registry.copyEnvironment(
            from: parent.id,
            to: "default-copy",
            rootImageIdentifier: "orlix.env.default-copy"
        )
        let copiedLayout = try registry.layout(forEnvironmentID: "default-copy")

        try Data("parent-base-image-after-copy".utf8)
            .write(to: parentLayout.baseImageURL)
        try Data("parent-state-image-after-copy".utf8)
            .write(to: parentLayout.stateImageURL)

        XCTAssertEqual(
            try Data(contentsOf: copiedLayout.baseImageURL),
            Data("parent-base-image-before-copy".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: copiedLayout.stateImageURL),
            Data("parent-state-image-before-copy".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: parentLayout.baseImageURL),
            Data("parent-base-image-after-copy".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: parentLayout.stateImageURL),
            Data("parent-state-image-after-copy".utf8)
        )
    }

    func testEnvironmentRootImageRequiresMaterializedBaseAndStateImages() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let descriptor = OrlixEnvironmentDescriptor(
            id: "alpine-materialized",
            source: .rootfsTar,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.alpine-materialized",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        try registry.save(descriptor)
        let layout = try registry.layout(forEnvironmentID: descriptor.id)

        XCTAssertThrowsError(
            try registry.materializedRootImage(
                forEnvironmentID: descriptor.id,
                kernelCommandLine: "console=hvc0"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .missingImage(layout.baseImageURL.path)
            )
        }

        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let rootImage = try registry.materializedRootImage(
            forEnvironmentID: descriptor.id,
            kernelCommandLine: "console=hvc0"
        )

        XCTAssertEqual(rootImage.environmentID, descriptor.id)
        XCTAssertEqual(rootImage.rootImageIdentifier, descriptor.rootImageIdentifier)
        XCTAssertEqual(rootImage.baseImageURL, layout.baseImageURL)
        XCTAssertEqual(rootImage.stateImageURL, layout.stateImageURL)
        XCTAssertEqual(rootImage.bootConfig.rootImageIdentifier, descriptor.rootImageIdentifier)
        XCTAssertEqual(rootImage.bootConfig.kernelCommandLine, "console=hvc0")
    }

    func testEnvironmentRootImageDefaultsToOverlayRootCommandLine() throws {
        let root = temporaryRegistryRoot()
        let descriptor = OrlixEnvironmentDescriptor(
            id: "alpine-overlay-default",
            source: .rootfsTar,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.alpine-overlay-default",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let rootImage = try OrlixEnvironmentRootImage.materialized(
            descriptor: descriptor,
            layout: layout
        )

        XCTAssertEqual(
            rootImage.bootConfig.kernelCommandLine,
            OrlixEnvironmentRootImage.defaultKernelCommandLine
                + " \(OrlixEnvironmentRootImage.defaultExecCommandLineKey)=/bin/sh"
                + " \(OrlixEnvironmentRootImage.defaultArgumentCommandLineKeyPrefix)0=/bin/sh"
                + " \(OrlixEnvironmentRootImage.defaultEnvironmentCommandLineKeyPrefix)0=PATH=/usr/bin:/bin"
                + " \(OrlixEnvironmentRootImage.defaultWorkingDirectoryCommandLineKey)=/"
                + " \(OrlixEnvironmentRootImage.defaultUserIDCommandLineKey)=0"
                + " \(OrlixEnvironmentRootImage.defaultGroupIDCommandLineKey)=0"
        )
        XCTAssertEqual(rootImage.bootConfig.profile, .development)
        XCTAssertTrue(
            try XCTUnwrap(rootImage.bootConfig.kernelCommandLine)
                .contains("rdinit=/init")
        )
        XCTAssertTrue(
            try XCTUnwrap(rootImage.bootConfig.kernelCommandLine)
                .contains("orlix.root=overlay")
        )
        XCTAssertTrue(
            try XCTUnwrap(rootImage.bootConfig.kernelCommandLine)
                .contains("orlix.exec=/bin/sh")
        )
    }

    func testEnvironmentRootImageBindsDefaultCommandExecutableToInit() throws {
        let root = temporaryRegistryRoot()
        let descriptor = OrlixEnvironmentDescriptor(
            id: "busybox-command",
            source: .rootfsTar,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.busybox-command",
            defaultCommand: ["/bin/busybox", "sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let rootImage = try OrlixEnvironmentRootImage.materialized(
            descriptor: descriptor,
            layout: layout
        )

        XCTAssertEqual(
            rootImage.bootConfig.kernelCommandLine,
            OrlixEnvironmentRootImage.defaultKernelCommandLine
                + " \(OrlixEnvironmentRootImage.defaultExecCommandLineKey)=/bin/busybox"
                + " \(OrlixEnvironmentRootImage.defaultArgumentCommandLineKeyPrefix)0=/bin/busybox"
                + " \(OrlixEnvironmentRootImage.defaultArgumentCommandLineKeyPrefix)1=sh"
                + " \(OrlixEnvironmentRootImage.defaultEnvironmentCommandLineKeyPrefix)0=PATH=/usr/bin:/bin"
                + " \(OrlixEnvironmentRootImage.defaultWorkingDirectoryCommandLineKey)=/"
                + " \(OrlixEnvironmentRootImage.defaultUserIDCommandLineKey)=0"
                + " \(OrlixEnvironmentRootImage.defaultGroupIDCommandLineKey)=0"
        )
    }

    func testEnvironmentRootImageBindsEncodedExecutionDefaultsToInit() throws {
        let root = temporaryRegistryRoot()
        let descriptor = OrlixEnvironmentDescriptor(
            id: "encoded-command",
            source: .ociLayout,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.encoded-command",
            defaultCommand: ["/bin/sh", "-c", "printf hello world"],
            defaultEnvironment: [
                "EMPTY": "",
                "MESSAGE": "hello world",
                "PATH": "/usr/bin:/bin"
            ],
            defaultWorkingDirectory: "/work/project",
            defaultUserID: 1000,
            defaultGroupID: 100
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let rootImage = try OrlixEnvironmentRootImage.materialized(
            descriptor: descriptor,
            layout: layout
        )
        let commandLine = try XCTUnwrap(rootImage.bootConfig.kernelCommandLine)

        XCTAssertTrue(commandLine.contains("orlix.exec=/bin/sh"))
        XCTAssertTrue(commandLine.contains("orlix.argv0=/bin/sh"))
        XCTAssertTrue(commandLine.contains("orlix.argv1=-c"))
        XCTAssertTrue(commandLine.contains("orlix.argv2=printf%20hello%20world"))
        XCTAssertTrue(commandLine.contains("orlix.env0=EMPTY="))
        XCTAssertTrue(commandLine.contains("orlix.env1=MESSAGE=hello%20world"))
        XCTAssertTrue(commandLine.contains("orlix.env2=PATH=/usr/bin:/bin"))
        XCTAssertTrue(commandLine.contains("orlix.cwd=/work/project"))
        XCTAssertTrue(commandLine.contains("orlix.uid=1000"))
        XCTAssertTrue(commandLine.contains("orlix.gid=100"))
    }

    func testEnvironmentRootImageEncodesLinuxPathDefaultsWithoutHostPathPolicy() throws {
        let root = temporaryRegistryRoot()
        let descriptor = OrlixEnvironmentDescriptor(
            id: "execution-linux-paths",
            source: .ociLayout,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.execution-linux-paths",
            defaultCommand: ["/usr/bin/../bin/sh", "", "arg with space"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/work/../project",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let rootImage = try OrlixEnvironmentRootImage.materialized(
            descriptor: descriptor,
            layout: layout
        )
        let commandLine = try XCTUnwrap(rootImage.bootConfig.kernelCommandLine)

        XCTAssertTrue(commandLine.contains("orlix.exec=/usr/bin/../bin/sh"))
        XCTAssertTrue(commandLine.contains("orlix.argv0=/usr/bin/../bin/sh"))
        XCTAssertTrue(commandLine.contains("orlix.argv1="))
        XCTAssertTrue(commandLine.contains("orlix.argv2=arg%20with%20space"))
        XCTAssertTrue(commandLine.contains("orlix.cwd=/work/../project"))
    }

    func testEnvironmentRootImageEncodesPathLookupCommandName() throws {
        let root = temporaryRegistryRoot()
        let descriptor = OrlixEnvironmentDescriptor(
            id: "execution-path-lookup",
            source: .ociLayout,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.execution-path-lookup",
            defaultCommand: ["sh", "-c", "printf path lookup"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let rootImage = try OrlixEnvironmentRootImage.materialized(
            descriptor: descriptor,
            layout: layout
        )
        let commandLine = try XCTUnwrap(rootImage.bootConfig.kernelCommandLine)

        XCTAssertTrue(commandLine.contains("orlix.exec=sh"))
        XCTAssertTrue(commandLine.contains("orlix.argv0=sh"))
        XCTAssertTrue(commandLine.contains("orlix.argv1=-c"))
        XCTAssertTrue(commandLine.contains("orlix.argv2=printf%20path%20lookup"))
        XCTAssertTrue(commandLine.contains("orlix.env0=PATH=/usr/bin:/bin"))
    }

    func testEnvironmentRootImageRejectsUnsafeDefaultCommandExecutable() throws {
        let root = temporaryRegistryRoot()
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "unsafe-command",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let descriptor = OrlixEnvironmentDescriptor(
            id: "unsafe-command",
            source: .rootfsTar,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.unsafe-command",
            defaultCommand: ["bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )

        XCTAssertThrowsError(
            try OrlixEnvironmentRootImage.materialized(
                descriptor: descriptor,
                layout: layout
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .invalidDefaultCommand("bin/sh")
            )
        }
    }

    func testEnvironmentRootImageRejectsUnsafeExecutionDefaults() throws {
        let root = temporaryRegistryRoot()
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "unsafe-execution-defaults",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let badEnv = OrlixEnvironmentDescriptor(
            id: "unsafe-execution-defaults",
            source: .ociLayout,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.unsafe-execution-defaults",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["BAD=KEY": "value"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentRootImage.materialized(
                descriptor: badEnv,
                layout: layout
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .invalidDefaultEnvironment("BAD=KEY")
            )
        }

        let badCwd = OrlixEnvironmentDescriptor(
            id: "unsafe-execution-defaults",
            source: .ociLayout,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.unsafe-execution-defaults",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: [:],
            defaultWorkingDirectory: "work",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        XCTAssertThrowsError(
            try OrlixEnvironmentRootImage.materialized(
                descriptor: badCwd,
                layout: layout
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .invalidDefaultWorkingDirectory("work")
            )
        }
    }

    func testEnvironmentRootImageRejectsUnimplementedExplicitMounts() throws {
        let root = temporaryRegistryRoot()
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "documents-mount-pending",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: layout.rootDirectory,
            withIntermediateDirectories: true
        )
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)
        let documentsMount = try OrlixEnvironmentMount.documents(
            targetPath: "/home/root/Documents"
        )
        let descriptor = OrlixEnvironmentDescriptor(
            id: "documents-mount-pending",
            source: .copiedEnvironment(parentID: "default"),
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.documents-mount-pending",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0,
            mounts: [documentsMount]
        )

        XCTAssertThrowsError(
            try OrlixEnvironmentRootImage.materialized(
                descriptor: descriptor,
                layout: layout
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .missingLinuxMountBackend(documentsMount)
            )
        }

        let externalMount = try OrlixEnvironmentMount.securityScopedExternal(
            bookmarkID: "project-folder-bookmark",
            targetPath: "/mnt/external/project",
            readOnly: true
        )
        let externalDescriptor = OrlixEnvironmentDescriptor(
            id: "documents-mount-pending",
            source: .copiedEnvironment(parentID: "default"),
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.documents-mount-pending",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0,
            mounts: [externalMount]
        )

        XCTAssertThrowsError(
            try OrlixEnvironmentRootImage.materialized(
                descriptor: externalDescriptor,
                layout: layout
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .missingLinuxMountBackend(externalMount)
            )
        }
    }

    func testEnvironmentRootImageRejectsMismatchedLayoutAndDirectories() throws {
        let root = temporaryRegistryRoot()
        let descriptor = OrlixEnvironmentDescriptor(
            id: "alpine-materialized",
            source: .rootfsTar,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.alpine-materialized",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "other-env",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )

        XCTAssertThrowsError(
            try OrlixEnvironmentRootImage.materialized(
                descriptor: descriptor,
                layout: layout,
                kernelCommandLine: "console=hvc0"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .environmentMismatch(
                    descriptorID: "alpine-materialized",
                    layoutID: "other-env"
                )
            )
        }

        let matchingLayout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        try FileManager.default.createDirectory(
            at: matchingLayout.baseImageURL,
            withIntermediateDirectories: true
        )
        try Data("state".utf8).write(to: matchingLayout.stateImageURL)

        XCTAssertThrowsError(
            try OrlixEnvironmentRootImage.materialized(
                descriptor: descriptor,
                layout: matchingLayout,
                kernelCommandLine: "console=hvc0"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentRootImageError,
                .imageIsDirectory(matchingLayout.baseImageURL.path)
            )
        }
    }

    func testLinuxSessionCanBindMaterializedEnvironmentRootImage() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let descriptor = OrlixEnvironmentDescriptor(
            id: "alpine-session",
            source: .rootfsTar,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.alpine-session",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        try registry.save(descriptor)
        let layout = try registry.layout(forEnvironmentID: descriptor.id)
        try Data("base".utf8).write(to: layout.baseImageURL)
        try Data("state".utf8).write(to: layout.stateImageURL)

        let rootImage = try registry.materializedRootImage(
            forEnvironmentID: descriptor.id,
            kernelCommandLine: "console=hvc0 root=/dev/vda rootfstype=ext4 ro"
        )
        let session = OrlixLinuxSession(materializedRootImage: rootImage)

        XCTAssertEqual(session.bootConfig, rootImage.bootConfig)
        XCTAssertEqual(
            session.bootConfig.rootImageIdentifier,
            "orlix.env.alpine-session"
        )
        XCTAssertEqual(
            session.bootConfig.kernelCommandLine,
            "console=hvc0 root=/dev/vda rootfstype=ext4 ro"
        )
    }

    func testMaterializedEnvironmentRootImageRegistersWithHostAdapter() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let descriptor = OrlixEnvironmentDescriptor(
            id: "alpine-register",
            source: .rootfsTar,
            platform: "linux/arm64",
            rootImageIdentifier: "orlix.env.alpine-register",
            defaultCommand: ["/bin/sh"],
            defaultEnvironment: ["PATH": "/usr/bin:/bin"],
            defaultWorkingDirectory: "/",
            defaultUserID: 0,
            defaultGroupID: 0
        )
        try registry.save(descriptor)
        let layout = try registry.layout(forEnvironmentID: descriptor.id)
        try Data(repeating: 0, count: 512).write(to: layout.baseImageURL)
        try Data(repeating: 0, count: 512).write(to: layout.stateImageURL)

        let rootImage = try registry.materializedRootImage(
            forEnvironmentID: descriptor.id,
            kernelCommandLine: "console=hvc0 root=/dev/vda rootfstype=ext4 ro"
        )

        let payloadRoot = root.appendingPathComponent(
            "Payload.bundle",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: payloadRoot,
            withIntermediateDirectories: true
        )

        XCTAssertTrue(
            rootImage.registerWithHostAdapterForTesting(
                payloadBundlePath: payloadRoot.path,
                initrdResource: "rootfs/initramfs.cpio.gz",
                baseBlockDevice: 0,
                stateBlockDevice: 1,
                stateBlockMinimumBytes: 1024
            )
        )
    }

    func testMaterializedRootRegistrationDoesNotRegisterAllBundledRootsFirst() throws {
        let sourceRoot = try repositoryRoot()
        let orlixOSSource = try String(
            contentsOf: sourceRoot
                .appendingPathComponent("OrlixOS/Sources/Session/OrlixOS.swift")
        )
        let methodRange = try XCTUnwrap(
            orlixOSSource.range(
                of: "static func registerMaterializedRootImage("
            )
        )
        let productResourcesRange = try XCTUnwrap(
            orlixOSSource[methodRange.lowerBound...].range(
                of: "private static var productResources"
            )
        )
        let methodBody = orlixOSSource[methodRange.lowerBound..<productResourcesRange.lowerBound]

        XCTAssertFalse(methodBody.contains("registerWithHostAdapter()"))
        XCTAssertTrue(methodBody.contains("orlix_host_resources_set_payload_root_path"))
        XCTAssertTrue(methodBody.contains("orlix_host_resources_clear_root_images()"))
        XCTAssertTrue(methodBody.contains("orlix_host_resources_register_root_image_files"))
    }

    func testEnvironmentImageMaterializationPlanMatchesProductExt4Shape() throws {
        let root = temporaryRegistryRoot()
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "alpine-ext4",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let stagingRoot = layout.importScratchDirectory
            .appendingPathComponent("staging-root", isDirectory: true)
        try FileManager.default.createDirectory(
            at: stagingRoot.appendingPathComponent("etc", isDirectory: true),
            withIntermediateDirectories: true
        )
        try Data("ID=alpine\n".utf8).write(
            to: stagingRoot.appendingPathComponent("etc/os-release")
        )

        let plan = try OrlixEnvironmentImageMaterializationPlan.plan(
            stagingRootDirectory: stagingRoot,
            storageLayout: layout
        )
        try plan.prepareInputTrees()

        XCTAssertEqual(plan.baseTreeDirectory.lastPathComponent, "base-tree")
        XCTAssertEqual(plan.stateTreeDirectory.lastPathComponent, "state-tree")
        XCTAssertEqual(plan.baseImageURL, layout.baseImageURL)
        XCTAssertEqual(plan.stateImageURL, layout.stateImageURL)
        XCTAssertEqual(plan.baseLabel, "ORLIXROOT")
        XCTAssertEqual(plan.stateLabel, "ORLIXSTATE")
        XCTAssertEqual(plan.rootOwner, "0:0")
        XCTAssertEqual(
            try String(
                contentsOf: plan.baseTreeDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: plan.baseTreeDirectory
                    .appendingPathComponent("dev").path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: plan.baseTreeDirectory
                    .appendingPathComponent("proc").path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: plan.baseTreeDirectory
                    .appendingPathComponent("sys").path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: plan.baseTreeDirectory
                    .appendingPathComponent("tmp").path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: plan.stateTreeDirectory
                    .appendingPathComponent("upper").path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: plan.stateTreeDirectory
                    .appendingPathComponent("work").path
            )
        )
    }

    func testEnvironmentImageMaterializationCommandsUseCanonicalMke2fsFlags() throws {
        let root = temporaryRegistryRoot()
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "alpine-ext4",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let plan = try OrlixEnvironmentImageMaterializationPlan.plan(
            stagingRootDirectory: layout.importScratchDirectory
                .appendingPathComponent("staging-root", isDirectory: true),
            storageLayout: layout,
            baseImageSize: "128m",
            stateImageSize: "64m"
        )

        let commands = try plan.commands(
            mke2fsExecutable: "/opt/e2fsprogs/bin/mke2fs",
            truncateExecutable: "/usr/bin/truncate",
            debugfsExecutable: "/opt/e2fsprogs/sbin/debugfs"
        )

        XCTAssertEqual(
            commands,
            [
                OrlixEnvironmentImageMaterializationCommand(
                    executable: "/usr/bin/truncate",
                    arguments: ["-s", "128m", layout.baseImageURL.path]
                ),
                OrlixEnvironmentImageMaterializationCommand(
                    executable: "/opt/e2fsprogs/bin/mke2fs",
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
                        "ORLIXROOT",
                        "-E",
                        "root_owner=0:0",
                        "-d",
                        plan.baseTreeDirectory.path,
                        layout.baseImageURL.path
                    ]
                ),
                OrlixEnvironmentImageMaterializationCommand(
                    executable: "/opt/e2fsprogs/sbin/debugfs",
                    arguments: [
                        "-w",
                        "-f",
                        plan.baseMetadataCommandsURL.path,
                        layout.baseImageURL.path
                    ]
                ),
                OrlixEnvironmentImageMaterializationCommand(
                    executable: "/usr/bin/truncate",
                    arguments: ["-s", "64m", layout.stateImageURL.path]
                ),
                OrlixEnvironmentImageMaterializationCommand(
                    executable: "/opt/e2fsprogs/bin/mke2fs",
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
                        "ORLIXSTATE",
                        "-E",
                        "root_owner=0:0",
                        "-d",
                        plan.stateTreeDirectory.path,
                        layout.stateImageURL.path
                    ]
                ),
                OrlixEnvironmentImageMaterializationCommand(
                    executable: "/opt/e2fsprogs/sbin/debugfs",
                    arguments: [
                        "-w",
                        "-f",
                        plan.stateMetadataCommandsURL.path,
                        layout.stateImageURL.path
                    ]
                )
            ]
        )
    }

    func testEnvironmentRootImageMakeTargetConsumesImporterMetadataCommands()
        throws
    {
        let sourceRoot = try repositoryRoot()
        let makefile = try String(
            contentsOf: sourceRoot.appendingPathComponent("OrlixOS/Makefile")
        )

        XCTAssertTrue(makefile.contains("ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS"))
        XCTAssertTrue(makefile.contains("ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS"))
        XCTAssertTrue(makefile.contains("missing ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS"))
        XCTAssertTrue(makefile.contains(#"state_metadata_commands="$$work_root/state-debugfs-metadata.commands""#))
        XCTAssertTrue(makefile.contains(#"ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS="$$state_metadata_commands""#))
        XCTAssertTrue(makefile.contains("set_inode_field %s mode 040755"))
    }

    func testEnvironmentImageMaterializationGeneratesManifestMetadataCommands() throws {
        let root = temporaryRegistryRoot()
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "alpine-ext4",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let plan = try OrlixEnvironmentImageMaterializationPlan.plan(
            stagingRootDirectory: layout.importScratchDirectory
                .appendingPathComponent("staging-root", isDirectory: true),
            storageLayout: layout
        )

        let commands = try plan.baseImageMetadataCommands(
            manifest: [
                OrlixRootfsTarManifestEntry(
                    path: "etc",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .directory,
                    linkName: nil
                ),
                OrlixRootfsTarManifestEntry(
                    path: #"home/orlix/has "quotes""#,
                    size: 4,
                    mode: 0o644,
                    uid: 1000,
                    gid: 1000,
                    type: .regularFile,
                    linkName: nil
                )
            ]
        )

        XCTAssertEqual(
            commands,
            [
                #"set_inode_field "/etc" uid 0"#,
                #"set_inode_field "/etc" gid 0"#,
                #"set_inode_field "/etc" mode 040755"#,
                #"set_inode_field "/home/orlix/has \"quotes\"" uid 1000"#,
                #"set_inode_field "/home/orlix/has \"quotes\"" gid 1000"#,
                #"set_inode_field "/home/orlix/has \"quotes\"" mode 0100644"#
            ]
        )
    }

    func testEnvironmentImageMaterializationRejectsUnsafeInputs() throws {
        let root = temporaryRegistryRoot()
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "alpine-ext4",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )

        XCTAssertThrowsError(
            try OrlixEnvironmentImageMaterializationPlan.plan(
                stagingRootDirectory: layout.importScratchDirectory,
                storageLayout: layout,
                baseImageSize: "../64m"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentImageMaterializationError,
                .invalidImageSize("../64m")
            )
        }

        let plan = try OrlixEnvironmentImageMaterializationPlan.plan(
            stagingRootDirectory: layout.importScratchDirectory,
            storageLayout: layout
        )
        XCTAssertThrowsError(
            try plan.commands(mke2fsExecutable: "")
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentImageMaterializationError,
                .invalidExecutable("")
            )
        }
        XCTAssertThrowsError(
            try plan.commands(debugfsExecutable: "")
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentImageMaterializationError,
                .invalidExecutable("")
            )
        }

        XCTAssertThrowsError(
            try plan.baseImageMetadataCommands(
                manifest: [
                    OrlixRootfsTarManifestEntry(
                        path: "bad\npath",
                        size: 0,
                        mode: 0o644,
                        uid: 0,
                        gid: 0,
                        type: .regularFile,
                        linkName: nil
                    )
                ]
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixEnvironmentImageMaterializationError,
                .invalidDebugfsPath("bad\npath")
            )
        }
    }

    func testRootfsTarEntryPathPolicyNormalizesSafeRelativePaths() throws {
        XCTAssertEqual(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("./etc/os-release"),
            "etc/os-release"
        )
        XCTAssertEqual(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("usr/bin/sh"),
            "usr/bin/sh"
        )
        XCTAssertEqual(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("./"),
            nil
        )
    }

    func testRootfsTarEntryPathPolicyRejectsUnsafePaths() {
        XCTAssertThrowsError(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("")
        )
        XCTAssertThrowsError(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("/etc/passwd")
        )
        XCTAssertThrowsError(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("../etc/passwd")
        )
        XCTAssertThrowsError(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("etc/../passwd")
        )
        XCTAssertThrowsError(
            try OrlixRootfsTarEntryPathPolicy.normalizedRelativePath("etc/passwd\u{0}")
        )
    }

    func testRootfsTarImportPlanBindsArchiveToEnvironmentStorage() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let archive = root.appendingPathComponent("alpine-rootfs.tar")

        let plan = try OrlixRootfsTarImportPlan.plan(
            archiveURL: archive,
            environmentID: "alpine-tar",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-tar"
        )

        XCTAssertEqual(plan.archiveURL, archive)
        XCTAssertEqual(plan.descriptor.id, "alpine-tar")
        XCTAssertEqual(plan.descriptor.source, .rootfsTar)
        XCTAssertEqual(plan.descriptor.platform, "linux/arm64")
        XCTAssertEqual(plan.descriptor.rootImageIdentifier, "orlix.env.alpine-tar")
        XCTAssertEqual(plan.storageLayout.environmentID, "alpine-tar")
        XCTAssertTrue(
            plan.storageLayout.baseImageURL.path.hasSuffix(
                "Application Support/Orlix/environments/alpine-tar/base.ext4"
            )
        )
    }

    func testRootfsTarManifestReaderParsesUstarEntries() throws {
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8)),
            TarFixtureEntry(path: "bin/sh", type: "2", linkName: "/usr/bin/busybox"),
            TarFixtureEntry(path: "sbin/init", type: "1", linkName: "bin/sh")
        ])

        let entries = try OrlixRootfsTarManifestReader().readManifest(from: data)

        XCTAssertEqual(
            entries,
            [
                OrlixRootfsTarManifestEntry(
                    path: "etc",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .directory,
                    linkName: nil
                ),
                OrlixRootfsTarManifestEntry(
                    path: "etc/os-release",
                    size: 10,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .regularFile,
                    linkName: nil
                ),
                OrlixRootfsTarManifestEntry(
                    path: "bin/sh",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .symbolicLink,
                    linkName: "/usr/bin/busybox"
                ),
                OrlixRootfsTarManifestEntry(
                    path: "sbin/init",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .hardLink,
                    linkName: "bin/sh"
                )
            ]
        )
    }

    func testRootfsTarManifestReaderParsesLinuxSpecialFiles() throws {
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "dev/null",
                type: "3",
                devMajor: 1,
                devMinor: 3
            ),
            TarFixtureEntry(
                path: "dev/vda",
                type: "4",
                devMajor: 254,
                devMinor: 0
            ),
            TarFixtureEntry(path: "run/initctl", type: "6")
        ])

        let entries = try OrlixRootfsTarManifestReader().readManifest(from: data)

        XCTAssertEqual(
            entries,
            [
                OrlixRootfsTarManifestEntry(
                    path: "dev/null",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .characterDevice,
                    linkName: nil,
                    deviceMajor: 1,
                    deviceMinor: 3
                ),
                OrlixRootfsTarManifestEntry(
                    path: "dev/vda",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .blockDevice,
                    linkName: nil,
                    deviceMajor: 254,
                    deviceMinor: 0
                ),
                OrlixRootfsTarManifestEntry(
                    path: "run/initctl",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .fifo,
                    linkName: nil
                )
            ]
        )
    }

    func testRootfsTarManifestReaderAppliesPAXExtendedHeaders() throws {
        let longPath = "usr/share/orlix/" + String(repeating: "component", count: 12)
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/long-file",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": longPath,
                    "uid": "1000",
                    "gid": "100",
                    "mode": "0640",
                    "SCHILY.xattr.security.selinux": "system_u:object_r:usr_t:s0",
                    "SCHILY.xattr.user.comment": "hello world",
                    "LIBARCHIVE.xattr.user.libarchive%2Ecomment": "bGliYXJjaGl2ZSB2YWx1ZQ=="
                ])
            ),
            TarFixtureEntry(path: "long-file", payload: Data("hello\n".utf8)),
            TarFixtureEntry(
                path: "PaxHeaders.0/tool",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "bin/tool",
                    "linkpath": "/usr/bin/busybox"
                ])
            ),
            TarFixtureEntry(path: "tool", type: "2", linkName: "ignored")
        ])

        let entries = try OrlixRootfsTarManifestReader().readManifest(from: data)

        XCTAssertEqual(
            entries,
            [
                OrlixRootfsTarManifestEntry(
                    path: longPath,
                    size: 6,
                    mode: 0o640,
                    uid: 1000,
                    gid: 100,
                    type: .regularFile,
                    linkName: nil,
                    extendedAttributes: [
                        "user.libarchive.comment": "libarchive value",
                        "security.selinux": "system_u:object_r:usr_t:s0",
                        "user.comment": "hello world"
                    ]
                ),
                OrlixRootfsTarManifestEntry(
                    path: "bin/tool",
                    size: 0,
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    type: .symbolicLink,
                    linkName: "/usr/bin/busybox"
                )
            ]
        )
    }

    func testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata() throws {
        let root = temporaryRegistryRoot()
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/os-release",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "etc/os-release",
                    "LIBARCHIVE.xattr.user.libarchive%2Ecomment": "bGliYXJjaGl2ZSB2YWx1ZQ==",
                    "SCHILY.xattr.security.selinux": "system_u:object_r:etc_t:s0",
                    "SCHILY.xattr.user.comment": "hello world"
                ])
            ),
            TarFixtureEntry(path: "os-release", payload: Data("ID=alpine\n".utf8))
        ])

        let entries = try OrlixRootfsTarMaterializer().materialize(data, into: root)

        XCTAssertEqual(entries.first?.path, "etc/os-release")
        XCTAssertEqual(
            entries.first?.extendedAttributes,
            [
                "security.selinux": "system_u:object_r:etc_t:s0",
                "user.comment": "hello world",
                "user.libarchive.comment": "libarchive value"
            ]
        )

        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "xattr-files",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let plan = try OrlixEnvironmentImageMaterializationPlan.plan(
            stagingRootDirectory: root,
            storageLayout: layout
        )
        let commands = try plan.baseImageMetadataCommands(manifest: entries)

        XCTAssertTrue(
            commands.contains(
                #"ea_set "/etc/os-release" security.selinux "system_u:object_r:etc_t:s0""#
            )
        )
        XCTAssertTrue(
            commands.contains(#"ea_set "/etc/os-release" user.comment "hello world""#)
        )
        XCTAssertTrue(
            commands.contains(
                #"ea_set "/etc/os-release" user.libarchive.comment "libarchive value""#
            )
        )
    }

    func testRootfsTarMaterializerAppliesGNUSparsePAXMap() throws {
        let root = temporaryRegistryRoot()
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/sparse-file",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "var/lib/orlix/sparse-file",
                    "GNU.sparse.map": "0,5,12,4",
                    "GNU.sparse.size": "16"
                ])
            ),
            TarFixtureEntry(
                path: "sparse-file",
                payload: Data("helloTAIL".utf8)
            )
        ])

        let entries = try OrlixRootfsTarMaterializer().materialize(data, into: root)
        let fileData = try Data(
            contentsOf: root.appendingPathComponent("var/lib/orlix/sparse-file")
        )

        XCTAssertEqual(entries.first?.path, "var/lib/orlix/sparse-file")
        XCTAssertEqual(
            entries.first?.sparseExtents,
            [
                OrlixRootfsTarSparseExtent(offset: 0, length: 5),
                OrlixRootfsTarSparseExtent(offset: 12, length: 4)
            ]
        )
        XCTAssertEqual(entries.first?.logicalSize, 16)
        XCTAssertEqual(fileData.count, 16)
        XCTAssertEqual(Array(fileData[0..<5]), Array(Data("hello".utf8)))
        XCTAssertEqual(Array(fileData[5..<12]), Array(repeating: 0, count: 7))
        XCTAssertEqual(Array(fileData[12..<16]), Array(Data("TAIL".utf8)))
    }

    func testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize() throws {
        let root = temporaryRegistryRoot()
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/sparse-file",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "var/lib/orlix/sparse-file",
                    "GNU.sparse.map": "12,8",
                    "GNU.sparse.size": "16"
                ])
            ),
            TarFixtureEntry(
                path: "sparse-file",
                payload: Data("toolong!".utf8)
            )
        ])

        XCTAssertThrowsError(
            try OrlixRootfsTarMaterializer().materialize(data, into: root)
        ) { error in
            XCTAssertEqual(
                error as? OrlixRootfsTarManifestError,
                .invalidPAXExtendedHeader
            )
        }
        XCTAssertFalse(FileManager.default.fileExists(atPath: root.path))
    }

    func testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch() throws {
        let root = temporaryRegistryRoot()
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/sparse-file",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "var/lib/orlix/sparse-file",
                    "GNU.sparse.map": "0,3",
                    "GNU.sparse.size": "3"
                ])
            ),
            TarFixtureEntry(
                path: "sparse-file",
                payload: Data("abcd".utf8)
            )
        ])

        XCTAssertThrowsError(
            try OrlixRootfsTarMaterializer().materialize(data, into: root)
        ) { error in
            XCTAssertEqual(
                error as? OrlixRootfsTarManifestError,
                .invalidPAXExtendedHeader
            )
        }
        XCTAssertFalse(FileManager.default.fileExists(atPath: root.path))
    }

    func testRootfsTarMaterializerAppliesPAXPathBeforeWriting() throws {
        let root = temporaryRegistryRoot()
        let paxPath = "opt/orlix/pax-materialized"
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/file",
                type: "x",
                payload: paxExtendedHeaderPayload(["path": paxPath])
            ),
            TarFixtureEntry(path: "file", payload: Data("from-pax\n".utf8))
        ])

        let entries = try OrlixRootfsTarMaterializer().materialize(data, into: root)

        XCTAssertEqual(entries.map(\.path), [paxPath])
        XCTAssertEqual(
            try String(contentsOf: root.appendingPathComponent(paxPath)),
            "from-pax\n"
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: root.appendingPathComponent("file").path
            )
        )
    }

    func testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting() throws {
        let root = temporaryRegistryRoot()
        let longPath = "usr/share/orlix/" + String(repeating: "long-path/", count: 9)
            + "payload"
        let longLink = "/usr/bin/" + String(repeating: "busybox-", count: 12)
            + "target"
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "././@LongLink",
                type: "L",
                payload: gnuLongNamePayload(longPath)
            ),
            TarFixtureEntry(path: "payload", payload: Data("gnu-path\n".utf8)),
            TarFixtureEntry(
                path: "././@LongLink",
                type: "K",
                payload: gnuLongNamePayload(longLink)
            ),
            TarFixtureEntry(path: "tool", type: "2", linkName: "ignored")
        ])

        let entries = try OrlixRootfsTarMaterializer().materialize(data, into: root)

        XCTAssertEqual(entries.map(\.path), [longPath, "tool"])
        XCTAssertEqual(
            try String(contentsOf: root.appendingPathComponent(longPath)),
            "gnu-path\n"
        )
        XCTAssertEqual(
            try FileManager.default.destinationOfSymbolicLink(
                atPath: root.appendingPathComponent("tool").path
            ),
            longLink
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: root.appendingPathComponent("payload").path
            )
        )
    }

    func testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly() throws {
        let root = temporaryRegistryRoot()
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "dev/", type: "5"),
            TarFixtureEntry(
                path: "dev/null",
                type: "3",
                devMajor: 1,
                devMinor: 3
            ),
            TarFixtureEntry(path: "run/initctl", type: "6")
        ])

        let entries = try OrlixRootfsTarMaterializer().materialize(data, into: root)

        XCTAssertEqual(entries.map(\.path), ["dev", "dev/null", "run/initctl"])
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: root.appendingPathComponent("dev").path
            )
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: root.appendingPathComponent("dev/null").path
            )
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: root.appendingPathComponent("run/initctl").path
            )
        )

        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: "special-files",
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent("Caches/Orlix", isDirectory: true),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let plan = try OrlixEnvironmentImageMaterializationPlan.plan(
            stagingRootDirectory: root,
            storageLayout: layout
        )
        let commands = try plan.baseImageMetadataCommands(manifest: entries)

        XCTAssertTrue(commands.contains("cd /dev"))
        XCTAssertTrue(commands.contains("mknod null c 1 3"))
        XCTAssertTrue(commands.contains("set_inode_field /dev/null mode 020755"))
        XCTAssertTrue(commands.contains("cd /run"))
        XCTAssertTrue(commands.contains("mknod initctl p"))
        XCTAssertTrue(
            commands.contains("set_inode_field /run/initctl mode 010755")
        )
    }

    func testRootfsTarManifestReaderParsesBase256NumericFields() throws {
        let payload = Data("base256\n".utf8)
        let data = tarArchive(entries: [
            TarFixtureEntry(
                path: "var/lib/orlix/base256",
                payload: payload,
                sizeEncoding: .base256,
                uid: 131_072,
                gid: 262_144,
                numericEncoding: .base256
            )
        ])

        let entries = try OrlixRootfsTarManifestReader().readManifest(from: data)

        XCTAssertEqual(
            entries,
            [
                OrlixRootfsTarManifestEntry(
                    path: "var/lib/orlix/base256",
                    size: UInt64(payload.count),
                    mode: 0o755,
                    uid: 131_072,
                    gid: 262_144,
                    type: .regularFile,
                    linkName: nil
                )
            ]
        )
    }

    func testRootfsTarManifestReaderRejectsUnsafeArchivePaths() {
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "../etc/passwd")
        ])

        XCTAssertThrowsError(
            try OrlixRootfsTarManifestReader().readManifest(from: data)
        )
    }

    func testRootfsTarManifestReaderRejectsUnsupportedTypesAndBadChecksums() {
        let unsupportedType = tarArchive(entries: [
            TarFixtureEntry(path: "unsupported", type: "7")
        ])
        XCTAssertThrowsError(
            try OrlixRootfsTarManifestReader().readManifest(from: unsupportedType)
        )

        var badChecksum = tarArchive(entries: [
            TarFixtureEntry(path: "etc/os-release")
        ])
        badChecksum[0] = UInt8(ascii: "x")
        XCTAssertThrowsError(
            try OrlixRootfsTarManifestReader().readManifest(from: badChecksum)
        )
    }

    func testRootfsTarMaterializerWritesValidatedEntriesToStagingTree() throws {
        let root = temporaryRegistryRoot()
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8)),
            TarFixtureEntry(path: "bin/sh", type: "2", linkName: "/usr/bin/busybox"),
            TarFixtureEntry(path: "etc/os-release-copy", type: "1", linkName: "etc/os-release")
        ])

        let entries = try OrlixRootfsTarMaterializer().materialize(
            data,
            into: root
        )

        XCTAssertEqual(entries.count, 4)
        XCTAssertEqual(
            try String(
                contentsOf: root.appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
        XCTAssertEqual(
            try FileManager.default.destinationOfSymbolicLink(
                atPath: root.appendingPathComponent("bin/sh").path
            ),
            "/usr/bin/busybox"
        )

        let originalAttributes = try FileManager.default.attributesOfItem(
            atPath: root.appendingPathComponent("etc/os-release").path
        )
        let hardlinkAttributes = try FileManager.default.attributesOfItem(
            atPath: root.appendingPathComponent("etc/os-release-copy").path
        )
        XCTAssertEqual(
            originalAttributes[.systemFileNumber] as? NSNumber,
            hardlinkAttributes[.systemFileNumber] as? NSNumber
        )
    }

    func testRootfsTarMaterializerRejectsUnsafeEntriesBeforeWriting() {
        let root = temporaryRegistryRoot()
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "../escape")
        ])

        XCTAssertThrowsError(
            try OrlixRootfsTarMaterializer().materialize(data, into: root)
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: root.appendingPathComponent("escape").path
            )
        )
    }

    func testRootfsTarMaterializerDoesNotLeavePartialRootOnMaterializationFailure() {
        let root = temporaryRegistryRoot()
            .appendingPathComponent("rootfs", isDirectory: true)
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8)),
            TarFixtureEntry(
                path: "etc/missing-copy",
                type: "1",
                linkName: "etc/does-not-exist"
            )
        ])

        XCTAssertThrowsError(
            try OrlixRootfsTarMaterializer().materialize(data, into: root)
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: root.appendingPathComponent("etc/os-release").path
            )
        )
        XCTAssertFalse(FileManager.default.fileExists(atPath: root.path))
        XCTAssertTrue(
            (try? FileManager.default.contentsOfDirectory(
                at: root.deletingLastPathComponent(),
                includingPropertiesForKeys: nil
            ).isEmpty) ?? false
        )
    }

    func testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let plan = try OrlixRootfsTarImportPlan.plan(
            archiveURL: root.appendingPathComponent("alpine-rootfs.tar"),
            environmentID: "alpine-import",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-import"
        )
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8))
        ])

        let result = try OrlixRootfsTarImporter().importArchiveData(
            data,
            using: plan,
            registry: registry
        )

        XCTAssertEqual(result.descriptor.id, "alpine-import")
        XCTAssertEqual(result.manifest.map(\.path), ["etc", "etc/os-release"])
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
        XCTAssertEqual(
            try registry.load(environmentID: "alpine-import"),
            result.descriptor
        )
        XCTAssertEqual(
            result.materializationPlan.stagingRootDirectory,
            result.stagingRootDirectory
        )
        XCTAssertEqual(
            result.materializationPlan.baseImageURL,
            result.storageLayout.baseImageURL
        )
        XCTAssertEqual(
            result.materializationPlan.stateImageURL,
            result.storageLayout.stateImageURL
        )
        XCTAssertEqual(
            try String(
                contentsOf: result.materializationPlan.baseTreeDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: result.materializationPlan.stateTreeDirectory
                    .appendingPathComponent("upper").path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: result.materializationPlan.stateTreeDirectory
                    .appendingPathComponent("work").path
            )
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains(#"set_inode_field "/etc" uid 0"#)
        )
        XCTAssertTrue(
            metadataCommands.contains(#"set_inode_field "/etc" mode 040755"#)
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/etc/os-release" mode 0100755"#
            )
        )
        let stateMetadataCommands = try String(
            contentsOf: result.materializationPlan.stateMetadataCommandsURL
        )
        XCTAssertTrue(
            stateMetadataCommands.contains("set_inode_field /upper mode 040755")
        )
        XCTAssertTrue(
            stateMetadataCommands.contains("set_inode_field /work mode 040755")
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.storageLayout.baseImageURL.path
            )
        )
    }

    func testRootfsTarImporterReadsArchiveFromImportPlanURL() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let archive = root.appendingPathComponent("alpine-rootfs.tar")
        try FileManager.default.createDirectory(
            at: root,
            withIntermediateDirectories: true
        )
        let plan = try OrlixRootfsTarImportPlan.plan(
            archiveURL: archive,
            environmentID: "alpine-file-import",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-file-import"
        )
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(
                path: "etc/os-release",
                payload: Data("ID=alpine-file-import\n".utf8)
            )
        ])
        try data.write(to: archive)

        let result = try OrlixRootfsTarImporter().importArchive(
            using: plan,
            registry: registry
        )

        XCTAssertEqual(result.descriptor.id, "alpine-file-import")
        XCTAssertEqual(result.manifest.map(\.path), ["etc", "etc/os-release"])
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine-file-import\n"
        )
        XCTAssertEqual(
            try registry.load(environmentID: "alpine-file-import"),
            result.descriptor
        )
        XCTAssertTrue(
            result.stagingRootDirectory.path.hasSuffix(
                "tmp/Orlix/imports/alpine-file-import/rootfs"
            )
        )
        XCTAssertTrue(
            result.storageLayout.rootDirectory.path.hasSuffix(
                "Application Support/Orlix/environments/alpine-file-import"
            )
        )
    }

    func testRootfsTarImporterRefusesExistingEnvironmentBeforeReadingArchive()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let existing = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.existing"
        )
        try registry.save(existing)
        let existingLayout = try registry.layout(forEnvironmentID: existing.id)
        try Data("existing-base-image".utf8).write(to: existingLayout.baseImageURL)
        try Data("existing-state-image".utf8).write(to: existingLayout.stateImageURL)
        let missingArchive = root.appendingPathComponent("missing-rootfs.tar")
        let plan = try OrlixRootfsTarImportPlan.plan(
            archiveURL: missingArchive,
            environmentID: existing.id,
            registry: registry,
            rootImageIdentifier: "orlix.env.imported"
        )

        XCTAssertThrowsError(
            try OrlixRootfsTarImporter().importArchive(
                using: plan,
                registry: registry
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixRootfsTarImportError,
                .destinationExists(existing.id)
            )
        }

        XCTAssertFalse(FileManager.default.fileExists(atPath: missingArchive.path))
        XCTAssertEqual(
            try registry.load(environmentID: existing.id),
            existing
        )
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.baseImageURL),
            Data("existing-base-image".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.stateImageURL),
            Data("existing-state-image".utf8)
        )
    }

    func testRootfsTarImporterCleansFailedImportBeforeRetry() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let plan = try OrlixRootfsTarImportPlan.plan(
            archiveURL: root.appendingPathComponent("broken-rootfs.tar"),
            environmentID: "broken-tar-import",
            registry: registry,
            rootImageIdentifier: "orlix.env.broken-tar-import"
        )
        let invalid = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(
                path: "etc/missing-hardlink",
                type: "1",
                linkName: "etc/does-not-exist"
            )
        ])

        XCTAssertThrowsError(
            try OrlixRootfsTarImporter().importArchiveData(
                invalid,
                using: plan,
                registry: registry
            )
        )
        XCTAssertFalse(
            FileManager.default.fileExists(atPath: plan.storageLayout.rootDirectory.path)
        )

        let valid = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=retry\n".utf8))
        ])
        let retry = try OrlixRootfsTarImporter().importArchiveData(
            valid,
            using: plan,
            registry: registry
        )

        XCTAssertEqual(retry.descriptor.id, "broken-tar-import")
        XCTAssertEqual(
            try String(
                contentsOf: retry.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=retry\n"
        )
        XCTAssertEqual(
            try registry.load(environmentID: "broken-tar-import"),
            retry.descriptor
        )
    }

    func testRootfsTarImporterDoesNotOverwriteExistingEnvironment() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let existing = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.existing"
        )
        try registry.save(existing)
        let existingLayout = try registry.layout(forEnvironmentID: existing.id)
        try Data("existing-base-image".utf8).write(to: existingLayout.baseImageURL)
        try Data("existing-state-image".utf8).write(to: existingLayout.stateImageURL)
        let plan = try OrlixRootfsTarImportPlan.plan(
            archiveURL: root.appendingPathComponent("default-rootfs.tar"),
            environmentID: existing.id,
            registry: registry,
            rootImageIdentifier: "orlix.env.imported"
        )
        let data = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=imported\n".utf8))
        ])

        XCTAssertThrowsError(
            try OrlixRootfsTarImporter().importArchiveData(
                data,
                using: plan,
                registry: registry
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixRootfsTarImportError,
                .destinationExists(existing.id)
            )
        }

        XCTAssertEqual(
            try registry.load(environmentID: existing.id),
            existing
        )
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.baseImageURL),
            Data("existing-base-image".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.stateImageURL),
            Data("existing-state-image".utf8)
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: existingLayout.importScratchDirectory
                    .appendingPathComponent("rootfs", isDirectory: true).path
            )
        )
    }

    func testOCIDigestSHA256MatchesKnownVector() {
        XCTAssertEqual(
            OrlixOCIDigest.sha256Hex(Data("abc".utf8)),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        )
    }

    func testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs() throws {
        let layout = try writeOCILayout()

        let imported = try OrlixOCIImageLayoutReader().readLayout(at: layout.root)

        XCTAssertEqual(imported.platform, "linux/arm64")
        XCTAssertEqual(imported.manifestDigest, layout.manifestDigest)
        XCTAssertEqual(imported.configDigest, layout.configDigest)
        XCTAssertEqual(
            imported.layers,
            [
                OrlixOCIImageLayer(
                    digest: layout.layerDigest,
                    mediaType: "application/vnd.oci.image.layer.v1.tar",
                    size: UInt64(layout.layerData[0].count)
                )
            ]
        )
        XCTAssertEqual(imported.processDefaults.environment["PATH"], "/usr/bin:/bin")
        XCTAssertEqual(imported.processDefaults.environment["EMPTY"], "")
        XCTAssertEqual(imported.processDefaults.entrypoint, ["/bin/sh"])
        XCTAssertEqual(imported.processDefaults.command, ["-c", "echo hello"])
        XCTAssertEqual(imported.processDefaults.workingDirectory, "/")
        XCTAssertEqual(imported.processDefaults.user, "0")
        XCTAssertEqual(imported.rootfsDiffIDs, [])
    }

    func testOCIImageLayoutReaderSelectsRequestedPlatformVariant() throws {
        let layout = try writeOCILayout(platformVariant: "v8")

        let imported = try OrlixOCIImageLayoutReader().readLayout(
            at: layout.root,
            platform: "linux/arm64/v8"
        )

        XCTAssertEqual(imported.platform, "linux/arm64/v8")
        XCTAssertEqual(imported.manifestDigest, layout.manifestDigest)
    }

    func testOCIImageLayoutReaderDoesNotSelectVariantForPlainPlatform() throws {
        let layout = try writeOCILayout(platformVariant: "v8")

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .missingPlatform("linux/arm64")
            )
        }
    }

    func testOCIImageLayoutReaderRejectsDigestMismatch() throws {
        let layout = try writeOCILayout()
        let blobURL = layout.root
            .appendingPathComponent("blobs/sha256", isDirectory: true)
            .appendingPathComponent(layout.layerDigest.split(separator: ":").last.map(String.init)!)
        try Data("changed".utf8).write(to: blobURL)

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        )
    }

    func testOCIImageLayoutReaderRejectsManifestSizeMismatch() throws {
        let layout = try writeOCILayout(manifestSizeOverride: 1)

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            guard case let .sizeMismatch(digest, expected, actual) =
                    error as? OrlixOCIImageLayoutError
            else {
                XCTFail("expected size mismatch, got \(error)")
                return
            }
            XCTAssertEqual(digest, layout.manifestDigest)
            XCTAssertEqual(expected, 1)
            XCTAssertGreaterThan(actual, expected)
        }
    }

    func testOCIImageLayoutReaderRejectsConfigSizeMismatch() throws {
        let layout = try writeOCILayout(configSizeOverride: 1)

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            guard case let .sizeMismatch(digest, expected, actual) =
                    error as? OrlixOCIImageLayoutError
            else {
                XCTFail("expected size mismatch, got \(error)")
                return
            }
            XCTAssertEqual(digest, layout.configDigest)
            XCTAssertEqual(expected, 1)
            XCTAssertGreaterThan(actual, expected)
        }
    }

    func testOCIImageLayoutReaderRejectsLayerSizeMismatch() throws {
        let layout = try writeOCILayout(layerSizeOverrides: [1])

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            guard case let .sizeMismatch(digest, expected, actual) =
                    error as? OrlixOCIImageLayoutError
            else {
                XCTFail("expected size mismatch, got \(error)")
                return
            }
            XCTAssertEqual(digest, layout.layerDigest)
            XCTAssertEqual(expected, 1)
            XCTAssertGreaterThan(actual, expected)
        }
    }

    func testOCIImageLayoutReaderAcceptsDockerSchema2DescriptorMediaTypes() throws {
        let layout = try writeOCILayout(
            manifestMediaType: "application/vnd.docker.distribution.manifest.v2+json",
            configMediaType: "application/vnd.docker.container.image.v1+json"
        )

        let imported = try OrlixOCIImageLayoutReader().readLayout(at: layout.root)

        XCTAssertEqual(imported.manifestDigest, layout.manifestDigest)
        XCTAssertEqual(imported.configDigest, layout.configDigest)
        XCTAssertEqual(imported.layers.map(\.digest), [layout.layerDigest])
    }

    func testOCIImageLayoutReaderRejectsUnsupportedManifestMediaType() throws {
        let mediaType = "application/vnd.example.image.manifest.v1+json"
        let layout = try writeOCILayout(manifestMediaType: mediaType)

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .unsupportedManifestMediaType(mediaType)
            )
        }
    }

    func testOCIImageLayoutReaderRejectsUnsupportedConfigMediaType() throws {
        let mediaType = "application/vnd.example.image.config.v1+json"
        let layout = try writeOCILayout(configMediaType: mediaType)

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .unsupportedConfigMediaType(mediaType)
            )
        }
    }

    func testOCIImageLayoutReaderParsesRootfsDiffIDs() throws {
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8))
        ])
        let diffID = "sha256:\(OrlixOCIDigest.sha256Hex(layer))"
        let layout = try writeOCILayout(
            layerData: [layer],
            rootfsDiffIDs: [diffID]
        )

        let imported = try OrlixOCIImageLayoutReader().readLayout(at: layout.root)

        XCTAssertEqual(imported.rootfsDiffIDs, [diffID])
    }

    func testOCIImageLayoutReaderAcceptsRootfsLayersTypeWithoutDiffIDs() throws {
        let layout = try writeOCILayout(
            rootfsType: "layers",
            rootfsDiffIDs: []
        )

        let imported = try OrlixOCIImageLayoutReader().readLayout(at: layout.root)

        XCTAssertEqual(imported.rootfsDiffIDs, [])
        XCTAssertEqual(imported.layers.map(\.digest), [layout.layerDigest])
    }

    func testOCIImageLayoutReaderRejectsUnsupportedRootfsType() throws {
        let layout = try writeOCILayout(
            rootfsType: "unsupported",
            rootfsDiffIDs: []
        )

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .unsupportedRootfsType("unsupported")
            )
        }
    }

    func testOCIImageLayoutReaderRejectsRootfsDiffIDCountMismatch() throws {
        let layout = try writeOCILayout(
            rootfsDiffIDs: [
                "sha256:\(String(repeating: "0", count: 64))",
                "sha256:\(String(repeating: "1", count: 64))"
            ]
        )

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(at: layout.root)
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .rootfsDiffIDCountMismatch(expected: 1, actual: 2)
            )
        }
    }

    func testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8))
        ])
        let expected = "sha256:\(String(repeating: "0", count: 64))"
        let actual = "sha256:\(OrlixOCIDigest.sha256Hex(layer))"
        let layout = try writeOCILayout(
            layerData: [layer],
            rootfsDiffIDs: [expected]
        )
        let storage = try registry.layout(forEnvironmentID: "alpine-bad-diffid")

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-bad-diffid",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-bad-diffid"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .rootfsDiffIDMismatch(
                    layerIndex: 0,
                    expected: expected,
                    actual: actual
                )
            )
        }
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: storage.importScratchDirectory
                    .appendingPathComponent("rootfs", isDirectory: true)
                    .path
            )
        )
        XCTAssertFalse(
            FileManager.default.fileExists(atPath: storage.rootDirectory.path)
        )

        let retryLayout = try writeOCILayout(
            layerData: [layer],
            rootfsDiffIDs: [actual]
        )
        let retry = try OrlixOCIImageLayoutImporter().importLayout(
            at: retryLayout.root,
            environmentID: "alpine-bad-diffid",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-bad-diffid"
        )
        XCTAssertEqual(retry.descriptor.id, "alpine-bad-diffid")
        XCTAssertEqual(
            try registry.load(environmentID: "alpine-bad-diffid"),
            retry.descriptor
        )
    }

    func testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let tarData = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8))
        ])
        let layout = try writeOCILayout(
            layerData: [try gzip(tarData)],
            layerMediaTypes: ["application/vnd.oci.image.layer.v1.tar+gzip"],
            rootfsDiffIDs: ["sha256:\(OrlixOCIDigest.sha256Hex(tarData))"]
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-gzip-diffid",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-gzip-diffid"
        )

        XCTAssertEqual(result.image.rootfsDiffIDs, [
            "sha256:\(OrlixOCIDigest.sha256Hex(tarData))"
        ])
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
    }

    func testOCIImageLayoutImporterBindsNumericUserAndGroup() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8))
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            user: "1000:100"
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-numeric-user",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-numeric-user"
        )

        XCTAssertEqual(result.image.processDefaults.user, "1000:100")
        XCTAssertEqual(result.descriptor.defaultUserID, 1000)
        XCTAssertEqual(result.descriptor.defaultGroupID, 100)
    }

    func testOCIImageLayoutImporterResolvesNamedUserFromImportedPasswd() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(
                path: "etc/passwd",
                payload: Data("app:x:1000:100:app:/home/app:/bin/sh\n".utf8)
            )
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            user: "app"
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-named-user",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-named-user"
        )

        XCTAssertEqual(result.image.processDefaults.user, "app")
        XCTAssertEqual(result.descriptor.defaultUserID, 1000)
        XCTAssertEqual(result.descriptor.defaultGroupID, 100)
    }

    func testOCIImageLayoutImporterResolvesNamedGroupFromImportedGroup() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(
                path: "etc/passwd",
                payload: Data("app:x:1000:100:app:/home/app:/bin/sh\n".utf8)
            ),
            TarFixtureEntry(
                path: "etc/group",
                payload: Data("staff:x:200:\n".utf8)
            )
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            user: "app:staff"
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-named-group",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-named-group"
        )

        XCTAssertEqual(result.image.processDefaults.user, "app:staff")
        XCTAssertEqual(result.descriptor.defaultUserID, 1000)
        XCTAssertEqual(result.descriptor.defaultGroupID, 200)
    }

    func testOCIImageLayoutImporterResolvesNamedGroupForNumericUser() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(
                path: "etc/group",
                payload: Data("staff:x:200:\n".utf8)
            )
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            user: "1000:staff"
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-numeric-user-named-group",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-numeric-user-named-group"
        )

        XCTAssertEqual(result.image.processDefaults.user, "1000:staff")
        XCTAssertEqual(result.descriptor.defaultUserID, 1000)
        XCTAssertEqual(result.descriptor.defaultGroupID, 200)
    }

    func testOCIImageLayoutImporterRejectsMissingNamedUser() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(
                path: "etc/passwd",
                payload: Data("app:x:1000:100:app:/home/app:/bin/sh\n".utf8)
            )
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            user: "missing"
        )

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-missing-user",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-missing-user"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .unsupportedUser("missing")
            )
        }
    }

    func testOCIImageLayoutImporterRejectsMissingNamedGroup() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(
                path: "etc/passwd",
                payload: Data("app:x:1000:100:app:/home/app:/bin/sh\n".utf8)
            ),
            TarFixtureEntry(
                path: "etc/group",
                payload: Data("staff:x:200:\n".utf8)
            )
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            user: "app:missing"
        )

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-missing-group",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-missing-group"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .unsupportedUser("app:missing")
            )
        }
    }

    func testOCIImageLayoutImporterBindsAbsoluteWorkingDirectory() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "work", type: "5"),
            TarFixtureEntry(path: "work/project", type: "5")
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            workingDirectory: "/work/project"
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-working-dir",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-working-dir"
        )

        XCTAssertEqual(result.image.processDefaults.workingDirectory, "/work/project")
        XCTAssertEqual(result.descriptor.defaultWorkingDirectory, "/work/project")
    }

    func testOCIImageLayoutImporterRejectsRelativeWorkingDirectory() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "work", type: "5")
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            workingDirectory: "work"
        )

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-relative-working-dir",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-relative-working-dir"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .invalidWorkingDirectory("work")
            )
        }
        XCTAssertThrowsError(
            try registry.load(environmentID: "alpine-relative-working-dir")
        )
    }

    func testOCIImageLayoutImporterBindsEnvironmentEntries() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8))
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            envEntries: [
                "PATH=/usr/local/bin:/usr/bin:/bin",
                "EMPTY=",
                "LANG=C.UTF-8"
            ]
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-env",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-env"
        )

        XCTAssertEqual(
            result.image.processDefaults.environment["PATH"],
            "/usr/local/bin:/usr/bin:/bin"
        )
        XCTAssertEqual(result.image.processDefaults.environment["EMPTY"], "")
        XCTAssertEqual(result.image.processDefaults.environment["LANG"], "C.UTF-8")
        XCTAssertEqual(
            result.descriptor.defaultEnvironment,
            result.image.processDefaults.environment
        )
    }

    func testOCIImageLayoutImporterRejectsMalformedEnvironmentEntry() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layout = try writeOCILayout(envEntries: ["PATH=/usr/bin:/bin", "BAD"])

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-bad-env",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-bad-env"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .invalidEnvironmentEntry("BAD")
            )
        }
        XCTAssertThrowsError(try registry.load(environmentID: "alpine-bad-env"))
    }

    func testOCIImageLayoutImporterRejectsEmptyEnvironmentKey() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layout = try writeOCILayout(envEntries: ["=value"])

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-empty-env-key",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-empty-env-key"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .invalidEnvironmentEntry("=value")
            )
        }
    }

    func testOCIImageLayoutImporterRejectsNulEnvironmentEntry() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layout = try writeOCILayout(envEntries: ["BAD\u{0}KEY=value"])

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-nul-env",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-nul-env"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .invalidEnvironmentEntry("BAD\u{0}KEY=value")
            )
        }
    }

    func testOCIImageLayoutImporterBindsEntrypointAndCommand() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "bin", type: "5"),
            TarFixtureEntry(path: "bin/app", payload: Data("app\n".utf8))
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            entrypoint: ["/bin/app"],
            command: ["--flag", "value"]
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-entrypoint-command",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-entrypoint-command"
        )

        XCTAssertEqual(result.image.processDefaults.entrypoint, ["/bin/app"])
        XCTAssertEqual(result.image.processDefaults.command, ["--flag", "value"])
        XCTAssertEqual(result.descriptor.defaultCommand, ["/bin/app", "--flag", "value"])
    }

    func testOCIImageLayoutImporterBindsCommandOnly() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "bin", type: "5"),
            TarFixtureEntry(path: "bin/echo", payload: Data("echo\n".utf8))
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            entrypoint: [],
            command: ["/bin/echo", "hello"]
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-command-only",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-command-only"
        )

        XCTAssertEqual(result.image.processDefaults.entrypoint, [])
        XCTAssertEqual(result.image.processDefaults.command, ["/bin/echo", "hello"])
        XCTAssertEqual(result.descriptor.defaultCommand, ["/bin/echo", "hello"])
    }

    func testOCIImageLayoutImporterDefaultsEmptyCommandToShell() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "bin", type: "5"),
            TarFixtureEntry(path: "bin/sh", payload: Data("sh\n".utf8))
        ])
        let layout = try writeOCILayout(
            layerData: [layer],
            entrypoint: [],
            command: []
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-empty-command",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-empty-command"
        )

        XCTAssertEqual(result.image.processDefaults.entrypoint, [])
        XCTAssertEqual(result.image.processDefaults.command, [])
        XCTAssertEqual(result.descriptor.defaultCommand, ["/bin/sh"])
    }

    func testOCIImageLayoutImporterRejectsNulEntrypoint() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layout = try writeOCILayout(entrypoint: ["/bin/sh\u{0}"], command: [])

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-nul-entrypoint",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-nul-entrypoint"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .invalidCommandEntry("/bin/sh\u{0}")
            )
        }
    }

    func testOCIImageLayoutImporterRejectsNulCommand() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layout = try writeOCILayout(command: ["-c", "echo\u{0}bad"])

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-nul-command",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-nul-command"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .invalidCommandEntry("echo\u{0}bad")
            )
        }
    }

    func testOCIImageLayoutReaderRejectsMissingPlatform() throws {
        let layout = try writeOCILayout()

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutReader().readLayout(
                at: layout.root,
                platform: "linux/amd64"
            )
        )
    }

    func testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let lowerLayer = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8)),
            TarFixtureEntry(path: "etc/remove-me", payload: Data("lower\n".utf8)),
            TarFixtureEntry(path: "etc/opaque/", type: "5"),
            TarFixtureEntry(path: "etc/opaque/old", payload: Data("old\n".utf8))
        ])
        let upperLayer = tarArchive(entries: [
            TarFixtureEntry(path: "etc/.wh.remove-me"),
            TarFixtureEntry(path: "etc/opaque/.wh..wh..opq"),
            TarFixtureEntry(path: "etc/opaque/new", payload: Data("new\n".utf8))
        ])
        let layout = try writeOCILayout(layerData: [lowerLayer, upperLayer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-oci",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-oci"
        )

        XCTAssertEqual(result.descriptor.id, "alpine-oci")
        XCTAssertEqual(result.descriptor.source, .ociLayout)
        XCTAssertEqual(result.descriptor.defaultCommand, ["/bin/sh", "-c", "echo hello"])
        XCTAssertEqual(
            result.manifest.map(\.path),
            ["etc", "etc/os-release", "etc/opaque", "etc/opaque/new"]
        )
        XCTAssertEqual(
            try registry.load(environmentID: "alpine-oci"),
            result.descriptor
        )
        XCTAssertEqual(
            result.materializationPlan.stagingRootDirectory,
            result.stagingRootDirectory
        )
        XCTAssertEqual(
            result.materializationPlan.baseImageURL,
            result.storageLayout.baseImageURL
        )
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("etc/remove-me").path
            )
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("etc/.wh.remove-me").path
            )
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("etc/opaque/old").path
            )
        )
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/opaque/new")
            ),
            "new\n"
        )
        XCTAssertEqual(
            try String(
                contentsOf: result.materializationPlan.baseTreeDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.materializationPlan.baseTreeDirectory
                    .appendingPathComponent("etc/remove-me").path
            )
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains(#"set_inode_field "/etc" uid 0"#)
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/etc/os-release" mode 0100755"#
            )
        )
        XCTAssertFalse(metadataCommands.contains(#""/etc/remove-me""#))
        XCTAssertFalse(metadataCommands.contains(#""/etc/opaque/old""#))
        XCTAssertFalse(metadataCommands.contains(".wh.remove-me"))
        let stateMetadataCommands = try String(
            contentsOf: result.materializationPlan.stateMetadataCommandsURL
        )
        XCTAssertTrue(
            stateMetadataCommands.contains("set_inode_field /upper mode 040755")
        )
        XCTAssertTrue(
            stateMetadataCommands.contains("set_inode_field /work mode 040755")
        )
    }

    func testOCIImageLayoutImporterAppliesRootOpaqueWhiteoutToManifest() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let lowerLayer = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/old", payload: Data("old\n".utf8)),
            TarFixtureEntry(path: "usr/", type: "5"),
            TarFixtureEntry(path: "usr/bin/", type: "5"),
            TarFixtureEntry(path: "usr/bin/tool", payload: Data("tool\n".utf8))
        ])
        let upperLayer = tarArchive(entries: [
            TarFixtureEntry(path: ".wh..wh..opq"),
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=reset\n".utf8))
        ])
        let layout = try writeOCILayout(layerData: [lowerLayer, upperLayer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-root-opaque",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-root-opaque"
        )

        XCTAssertEqual(result.manifest.map(\.path), ["etc", "etc/os-release"])
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=reset\n"
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("usr/bin/tool").path
            )
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertFalse(metadataCommands.contains(#""/etc/old""#))
        XCTAssertFalse(metadataCommands.contains(#""/usr""#))
        XCTAssertFalse(metadataCommands.contains(#""/usr/bin/tool""#))
    }

    func testOCIImageLayoutImporterRecordsImplicitOpaqueDirectoryInManifest()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let lowerLayer = tarArchive(entries: [
            TarFixtureEntry(
                path: "var/cache/old",
                payload: Data("old\n".utf8)
            )
        ])
        let upperLayer = tarArchive(entries: [
            TarFixtureEntry(path: "var/cache/.wh..wh..opq"),
            TarFixtureEntry(
                path: "var/cache/new",
                payload: Data("new\n".utf8)
            )
        ])
        let layout = try writeOCILayout(layerData: [lowerLayer, upperLayer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-implicit-opaque-dir",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-implicit-opaque-dir"
        )

        XCTAssertEqual(result.manifest.map(\.path), ["var/cache", "var/cache/new"])
        XCTAssertEqual(result.manifest.first?.type, .directory)
        XCTAssertEqual(result.manifest.first?.mode, 0o755)
        XCTAssertEqual(result.manifest.first?.uid, 0)
        XCTAssertEqual(result.manifest.first?.gid, 0)
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("var/cache/old").path
            )
        )
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("var/cache/new")
            ),
            "new\n"
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains(#"set_inode_field "/var/cache" mode 040755"#)
        )
        XCTAssertFalse(metadataCommands.contains(#""/var/cache/old""#))
    }

    func testOCIImageLayoutImporterPreservesChildrenWhenDirectoryEntryComesLater()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(
                path: "etc/os-release",
                payload: Data("ID=late-dir\n".utf8)
            ),
            TarFixtureEntry(path: "etc/", type: "5")
        ])
        let layout = try writeOCILayout(layerData: [layer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-late-dir-entry",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-late-dir-entry"
        )

        XCTAssertEqual(result.manifest.map(\.path), ["etc/os-release", "etc"])
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=late-dir\n"
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/etc/os-release" mode 0100755"#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(#"set_inode_field "/etc" mode 040755"#)
        )
    }

    func testOCIImageLayoutImporterHonorsWhiteoutOrderWithinLayer()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(
                path: "etc/transient",
                payload: Data("transient\n".utf8)
            ),
            TarFixtureEntry(path: "etc/.wh.transient"),
            TarFixtureEntry(
                path: "etc/final",
                payload: Data("final\n".utf8)
            )
        ])
        let layout = try writeOCILayout(layerData: [layer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-whiteout-order",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-whiteout-order"
        )

        XCTAssertEqual(result.manifest.map(\.path), ["etc", "etc/final"])
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("etc/transient").path
            )
        )
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/final")
            ),
            "final\n"
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertFalse(metadataCommands.contains(#""/etc/transient""#))
        XCTAssertFalse(metadataCommands.contains(".wh.transient"))
        XCTAssertTrue(
            metadataCommands.contains(#"set_inode_field "/etc/final" mode 0100755"#)
        )
    }

    func testOCIImageLayoutImporterWhiteoutRemovesDanglingSymlink()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let lowerLayer = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(
                path: "etc/dangling",
                type: "2",
                linkName: "/missing-target"
            )
        ])
        let upperLayer = tarArchive(entries: [
            TarFixtureEntry(path: "etc/.wh.dangling")
        ])
        let layout = try writeOCILayout(layerData: [lowerLayer, upperLayer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-whiteout-dangling-symlink",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-whiteout-dangling-symlink"
        )

        XCTAssertEqual(result.manifest.map(\.path), ["etc"])
        XCTAssertThrowsError(
            try FileManager.default.destinationOfSymbolicLink(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("etc/dangling").path
            )
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertFalse(metadataCommands.contains("dangling"))
        XCTAssertFalse(metadataCommands.contains(".wh.dangling"))
    }

    func testOCIImageLayoutImporterDoesNotOverwriteExistingEnvironment()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let existing = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.existing"
        )
        try registry.save(existing)
        let existingLayout = try registry.layout(forEnvironmentID: existing.id)
        try Data("existing-base-image".utf8).write(to: existingLayout.baseImageURL)
        try Data("existing-state-image".utf8).write(to: existingLayout.stateImageURL)
        let ociLayout = try writeOCILayout(layerData: [
            tarArchive(entries: [
                TarFixtureEntry(path: "etc/", type: "5"),
                TarFixtureEntry(
                    path: "etc/os-release",
                    payload: Data("ID=oci\n".utf8)
                )
            ])
        ])

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: ociLayout.root,
                environmentID: existing.id,
                registry: registry,
                rootImageIdentifier: "orlix.env.imported"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .destinationExists(existing.id)
            )
        }

        XCTAssertEqual(
            try registry.load(environmentID: existing.id),
            existing
        )
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.baseImageURL),
            Data("existing-base-image".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.stateImageURL),
            Data("existing-state-image".utf8)
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: existingLayout.importScratchDirectory
                    .appendingPathComponent("rootfs", isDirectory: true).path
            )
        )
    }

    func testOCIImageLayoutImporterRefusesExistingEnvironmentBeforeReadingLayout()
        throws
    {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let existing = OrlixEnvironmentDescriptor.defaultEnvironment(
            rootImageIdentifier: "orlix.env.existing"
        )
        try registry.save(existing)
        let existingLayout = try registry.layout(forEnvironmentID: existing.id)
        try Data("existing-base-image".utf8).write(to: existingLayout.baseImageURL)
        try Data("existing-state-image".utf8).write(to: existingLayout.stateImageURL)
        let missingLayout = root.appendingPathComponent(
            "missing-oci-layout",
            isDirectory: true
        )

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: missingLayout,
                environmentID: existing.id,
                registry: registry,
                rootImageIdentifier: "orlix.env.imported"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixOCIImageLayoutError,
                .destinationExists(existing.id)
            )
        }

        XCTAssertFalse(FileManager.default.fileExists(atPath: missingLayout.path))
        XCTAssertEqual(try registry.load(environmentID: existing.id), existing)
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.baseImageURL),
            Data("existing-base-image".utf8)
        )
        XCTAssertEqual(
            try Data(contentsOf: existingLayout.stateImageURL),
            Data("existing-state-image".utf8)
        )
    }

    func testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let tarData = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=alpine\n".utf8))
        ])
        let layout = try writeOCILayout(
            layerData: [try gzip(tarData)],
            layerMediaTypes: ["application/vnd.oci.image.layer.v1.tar+gzip"]
        )

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-gzip-oci",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-gzip-oci"
        )

        XCTAssertEqual(result.descriptor.source, .ociLayout)
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("etc/os-release")
            ),
            "ID=alpine\n"
        )
    }

    func testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let paxPath = "usr/share/orlix/oci-pax-file"
        let layer = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/oci-pax-file",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": paxPath,
                    "uid": "1000",
                    "gid": "100",
                    "mode": "0640",
                    "LIBARCHIVE.xattr.user.oci%2Ecomment": "b2NpIGxpYmFyY2hpdmU=",
                    "SCHILY.xattr.security.selinux": "system_u:object_r:usr_t:s0"
                ])
            ),
            TarFixtureEntry(path: "oci-pax-file", payload: Data("oci-pax\n".utf8)),
            TarFixtureEntry(
                path: "PaxHeaders.0/oci-tool",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "bin/oci-tool",
                    "linkpath": "/usr/bin/busybox",
                    "mode": "0777"
                ])
            ),
            TarFixtureEntry(path: "oci-tool", type: "2", linkName: "ignored")
        ])
        let layout = try writeOCILayout(layerData: [layer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-oci-pax",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-oci-pax"
        )

        XCTAssertEqual(result.manifest.map(\.path), [paxPath, "bin/oci-tool"])
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent(paxPath)
            ),
            "oci-pax\n"
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("oci-pax-file").path
            )
        )
        XCTAssertEqual(
            try FileManager.default.destinationOfSymbolicLink(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("bin/oci-tool").path
            ),
            "/usr/bin/busybox"
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/usr/share/orlix/oci-pax-file" uid 1000"#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/usr/share/orlix/oci-pax-file" gid 100"#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/usr/share/orlix/oci-pax-file" mode 0100640"#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"ea_set "/usr/share/orlix/oci-pax-file" security.selinux "system_u:object_r:usr_t:s0""#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"ea_set "/usr/share/orlix/oci-pax-file" user.oci.comment "oci libarchive""#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/bin/oci-tool" mode 0120777"#
            )
        )
    }

    func testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let longPath = "usr/share/orlix/" + String(repeating: "oci-long/", count: 10)
            + "payload"
        let longLink = "/usr/bin/" + String(repeating: "busybox-", count: 12)
            + "target"
        let layer = tarArchive(entries: [
            TarFixtureEntry(
                path: "././@LongLink",
                type: "L",
                payload: gnuLongNamePayload(longPath)
            ),
            TarFixtureEntry(path: "payload", payload: Data("oci-gnu\n".utf8)),
            TarFixtureEntry(
                path: "././@LongLink",
                type: "K",
                payload: gnuLongNamePayload(longLink)
            ),
            TarFixtureEntry(path: "tool", type: "2", linkName: "ignored")
        ])
        let layout = try writeOCILayout(layerData: [layer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-oci-gnu",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-oci-gnu"
        )

        XCTAssertEqual(result.manifest.map(\.path), [longPath, "tool"])
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent(longPath)
            ),
            "oci-gnu\n"
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("payload").path
            )
        )
        XCTAssertEqual(
            try FileManager.default.destinationOfSymbolicLink(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("tool").path
            ),
            longLink
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/\#(longPath)" mode 0100755"#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/tool" mode 0120755"#
            )
        )
    }

    func testOCIImageLayoutImporterCarriesBase256NumericMetadata() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(
                path: "var/lib/orlix/base256",
                payload: Data("oci-base256\n".utf8),
                uid: 131_072,
                gid: 262_144,
                numericEncoding: .base256
            )
        ])
        let layout = try writeOCILayout(layerData: [layer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-oci-base256",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-oci-base256"
        )

        XCTAssertEqual(result.manifest.first?.uid, 131_072)
        XCTAssertEqual(result.manifest.first?.gid, 262_144)
        XCTAssertEqual(
            try String(
                contentsOf: result.stagingRootDirectory
                    .appendingPathComponent("var/lib/orlix/base256")
            ),
            "oci-base256\n"
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/var/lib/orlix/base256" uid 131072"#
            )
        )
        XCTAssertTrue(
            metadataCommands.contains(
                #"set_inode_field "/var/lib/orlix/base256" gid 262144"#
            )
        )
    }

    func testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(
                path: "dev/null",
                type: "3",
                devMajor: 1,
                devMinor: 3
            ),
            TarFixtureEntry(path: "run/initctl", type: "6")
        ])
        let layout = try writeOCILayout(layerData: [layer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-oci-special-files",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-oci-special-files"
        )

        XCTAssertEqual(result.manifest.map(\.path), ["dev/null", "run/initctl"])
        XCTAssertEqual(result.manifest.first?.type, .characterDevice)
        XCTAssertEqual(result.manifest.first?.deviceMajor, 1)
        XCTAssertEqual(result.manifest.first?.deviceMinor, 3)
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("dev/null").path
            )
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: result.stagingRootDirectory
                    .appendingPathComponent("run/initctl").path
            )
        )
        let metadataCommands = try String(
            contentsOf: result.materializationPlan.baseMetadataCommandsURL
        )
        XCTAssertTrue(
            metadataCommands.contains("cd /dev\nmknod null c 1 3")
        )
        XCTAssertTrue(
            metadataCommands.contains("set_inode_field /dev/null mode 020755")
        )
        XCTAssertTrue(
            metadataCommands.contains("cd /run\nmknod initctl p")
        )
        XCTAssertTrue(
            metadataCommands.contains(
                "set_inode_field /run/initctl mode 010755"
            )
        )
    }

    func testOCIImageLayoutImporterAppliesGNUSparsePAXMap() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/oci-sparse",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "var/lib/orlix/oci-sparse",
                    "GNU.sparse.map": "0,3,10,6",
                    "GNU.sparse.size": "16"
                ])
            ),
            TarFixtureEntry(
                path: "oci-sparse",
                payload: Data("abcXYZ123".utf8)
            )
        ])
        let layout = try writeOCILayout(layerData: [layer])

        let result = try OrlixOCIImageLayoutImporter().importLayout(
            at: layout.root,
            environmentID: "alpine-oci-sparse",
            registry: registry,
            rootImageIdentifier: "orlix.env.alpine-oci-sparse"
        )
        let fileData = try Data(
            contentsOf: result.stagingRootDirectory
                .appendingPathComponent("var/lib/orlix/oci-sparse")
        )

        XCTAssertEqual(result.manifest.first?.path, "var/lib/orlix/oci-sparse")
        XCTAssertEqual(
            result.manifest.first?.sparseExtents,
            [
                OrlixRootfsTarSparseExtent(offset: 0, length: 3),
                OrlixRootfsTarSparseExtent(offset: 10, length: 6)
            ]
        )
        XCTAssertEqual(result.manifest.first?.logicalSize, 16)
        XCTAssertEqual(fileData.count, 16)
        XCTAssertEqual(Array(fileData[0..<3]), Array(Data("abc".utf8)))
        XCTAssertEqual(Array(fileData[3..<10]), Array(repeating: 0, count: 7))
        XCTAssertEqual(Array(fileData[10..<16]), Array(Data("XYZ123".utf8)))
    }

    func testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layer = tarArchive(entries: [
            TarFixtureEntry(
                path: "PaxHeaders.0/oci-sparse",
                type: "x",
                payload: paxExtendedHeaderPayload([
                    "path": "var/lib/orlix/oci-sparse",
                    "GNU.sparse.map": "0,3",
                    "GNU.sparse.size": "3"
                ])
            ),
            TarFixtureEntry(
                path: "oci-sparse",
                payload: Data("abcd".utf8)
            )
        ])
        let layout = try writeOCILayout(layerData: [layer])
        let storage = try registry.layout(forEnvironmentID: "alpine-oci-bad-sparse")

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: layout.root,
                environmentID: "alpine-oci-bad-sparse",
                registry: registry,
                rootImageIdentifier: "orlix.env.alpine-oci-bad-sparse"
            )
        ) { error in
            XCTAssertEqual(
                error as? OrlixRootfsTarManifestError,
                .invalidPAXExtendedHeader
            )
        }
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: storage.importScratchDirectory
                    .appendingPathComponent("rootfs", isDirectory: true)
                    .path
            )
        )
    }

    func testOCIImageLayoutImporterDoesNotCommitPartialRootOnLayerFailure() throws {
        let root = temporaryRegistryRoot()
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: root.appendingPathComponent(
                "Application Support/Orlix",
                isDirectory: true
            ),
            cacheRoot: root.appendingPathComponent(
                "Caches/Orlix",
                isDirectory: true
            ),
            scratchRoot: root.appendingPathComponent("tmp/Orlix", isDirectory: true)
        )
        let layout = try registry.layout(forEnvironmentID: "broken-oci")
        let stagingRoot = layout.importScratchDirectory
            .appendingPathComponent("rootfs", isDirectory: true)
        try FileManager.default.createDirectory(
            at: stagingRoot.appendingPathComponent("etc", isDirectory: true),
            withIntermediateDirectories: true
        )
        try Data("ID=previous\n".utf8).write(
            to: stagingRoot.appendingPathComponent("etc/os-release")
        )

        let lowerLayer = tarArchive(entries: [
            TarFixtureEntry(path: "etc/", type: "5"),
            TarFixtureEntry(path: "etc/os-release", payload: Data("ID=new\n".utf8))
        ])
        let failingUpperLayer = tarArchive(entries: [
            TarFixtureEntry(
                path: "etc/missing-hardlink",
                type: "1",
                linkName: "etc/does-not-exist"
            )
        ])
        let ociLayout = try writeOCILayout(
            layerData: [lowerLayer, failingUpperLayer]
        )

        XCTAssertThrowsError(
            try OrlixOCIImageLayoutImporter().importLayout(
                at: ociLayout.root,
                environmentID: "broken-oci",
                registry: registry,
                rootImageIdentifier: "orlix.env.broken-oci"
            )
        )
        XCTAssertEqual(
            try String(
                contentsOf: stagingRoot.appendingPathComponent("etc/os-release")
            ),
            "ID=previous\n"
        )
        XCTAssertFalse(
            FileManager.default.fileExists(
                atPath: stagingRoot
                    .appendingPathComponent("etc/missing-hardlink")
                    .path
            )
        )
        let scratchContents = try FileManager.default.contentsOfDirectory(
            at: layout.importScratchDirectory,
            includingPropertiesForKeys: nil
        )
        XCTAssertFalse(
            scratchContents.contains {
                $0.lastPathComponent.hasPrefix(".rootfs.oci-import-")
            }
        )
    }

    func testOCILayerDecoderSupportsGzipMediaType() throws {
        let gzipHello = Data([
            0x1f, 0x8b, 0x08, 0x00, 0xca, 0xd5, 0x29, 0x6a,
            0x00, 0x03, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07,
            0x00, 0x86, 0xa6, 0x10, 0x36, 0x05, 0x00, 0x00,
            0x00
        ])

        let decoded = try OrlixOCILayerDecoder().decode(
            layerData: gzipHello,
            mediaType: "application/vnd.oci.image.layer.v1.tar+gzip"
        )

        XCTAssertEqual(decoded, Data("hello".utf8))
    }

    func testOCIImageLayoutImporterRejectsZstdLayerMediaTypesUntilDecoderExists() throws {
        let mediaTypes = [
            "application/vnd.oci.image.layer.v1.tar+zstd",
            "application/vnd.oci.image.layer.nondistributable.v1.tar+zstd",
            "application/vnd.docker.image.rootfs.diff.tar.zstd"
        ]

        for (index, mediaType) in mediaTypes.enumerated() {
            let layout = try writeOCILayout(
                layerData: [Data("not-zstd".utf8)],
                layerMediaTypes: [mediaType]
            )

            XCTAssertThrowsError(
                try OrlixOCIImageLayoutImporter().importLayout(
                    at: layout.root,
                    environmentID: "compressed-\(index)",
                    registry: OrlixEnvironmentRegistry(
                        linuxStateRoot: temporaryRegistryRoot(),
                        cacheRoot: temporaryRegistryRoot(),
                        scratchRoot: temporaryRegistryRoot()
                    ),
                    rootImageIdentifier: "orlix.env.compressed"
                )
            ) { error in
                XCTAssertEqual(
                    error as? OrlixOCIImageLayoutError,
                    .unsupportedLayerMediaType(mediaType)
                )
            }
        }
    }

    func testStoragePolicyKeepsLinuxStateCacheScratchAndDocumentsSeparate() throws {
        let policy = OrlixStoragePolicy.current
        let applicationSupport = try FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: false
        )
        let caches = try FileManager.default.url(
            for: .cachesDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: false
        )
        let linuxStateDirectory = try policy.linuxStateDirectory()
        let cacheDirectory = try policy.cacheDirectory()
        let scratchDirectory = policy.scratchDirectory()

        XCTAssertEqual(
            linuxStateDirectory,
            applicationSupport.appendingPathComponent("Orlix", isDirectory: true)
        )
        XCTAssertEqual(
            cacheDirectory,
            caches.appendingPathComponent("Orlix", isDirectory: true)
        )
        XCTAssertEqual(
            scratchDirectory,
            FileManager.default.temporaryDirectory
                .appendingPathComponent("Orlix", isDirectory: true)
        )
        XCTAssertNotEqual(linuxStateDirectory, cacheDirectory)
        XCTAssertNotEqual(linuxStateDirectory, scratchDirectory)
        XCTAssertNotEqual(cacheDirectory, scratchDirectory)
        XCTAssertEqual(policy.linuxTemporaryFilesystemType, "tmpfs")
        XCTAssertEqual(policy.documentsMountPolicy, .explicitMountOnly)
    }

    func testRootfsSourcesKeepTmpLinuxOwnedAndDocumentsOutOfRootTruth() throws {
        let sourceRoot = try repositoryRoot()
        let initSource = try String(
            contentsOf: sourceRoot
                .appendingPathComponent("OrlixOS/Sources/init/init.c")
        )
        let rootfsMakefile = try String(
            contentsOf: sourceRoot
                .appendingPathComponent("OrlixOS/Sources/make/rootfs.mk")
        )

        XCTAssertTrue(initSource.contains(#"mount_if_needed("tmpfs", "/tmp""#))
        XCTAssertTrue(rootfsMakefile.contains(#""$$root_tree/tmp""#))
        XCTAssertTrue(rootfsMakefile.contains(#"chmod 1777 "$$root_tree/tmp""#))
        XCTAssertFalse(initSource.contains("Documents"))
        XCTAssertFalse(rootfsMakefile.contains("Documents"))
    }

    func testPayloadBundleIsResolvedFromOrlixOSTargetMetadata() throws {
        let payloadURL = try XCTUnwrap(OrlixOSPayload.bundleURL)
        let profile = try XCTUnwrap(OrlixOSPayload.selectedBootProfile)
        let kernelCommandLine = try XCTUnwrap(OrlixOSPayload.kernelCommandLine)

        XCTAssertTrue(FileManager.default.fileExists(atPath: payloadURL.path))
        XCTAssertTrue(profile == .release || profile == .development)
        XCTAssertTrue(kernelCommandLine.contains("console=ttyS0"))
        XCTAssertTrue(kernelCommandLine.contains("console=hvc0"))
    }

    func testRootImageDescriptorsComeFromOrlixOSTargetMetadata() throws {
        let productRootIdentifier = try XCTUnwrap(
            OrlixOSPayload.productRootImageIdentifier
        )
        let descriptors = OrlixOSPayload.rootImageDescriptors

        XCTAssertFalse(productRootIdentifier.isEmpty)
        XCTAssertTrue(
            descriptors.contains { $0.identifier == productRootIdentifier }
        )
        let upstreamTestDescriptors = descriptors.filter {
            $0.initrdBundleName != nil
        }
        XCTAssertFalse(upstreamTestDescriptors.isEmpty)
        for descriptor in upstreamTestDescriptors {
            XCTAssertFalse(descriptor.role.isEmpty)
            XCTAssertFalse(descriptor.identifier.isEmpty)
            let kernelCommandLine = try XCTUnwrap(descriptor.kernelCommandLine)
            XCTAssertTrue(kernelCommandLine.contains("rdinit=/init"))
            XCTAssertTrue(kernelCommandLine.contains("orlix.root=initramfs-only"))
            XCTAssertNotNil(descriptor.initrdBundleName)
            XCTAssertNotNil(descriptor.initrdBundleExtension)
            XCTAssertNotNil(descriptor.initrdResource)
        }
    }

    func testSingleTerminalSessionCarriesInputAndOutput() {
        let transport = RecordingTerminalTransport()
        let session = OrlixTerminalSession(transport: transport)
        let receivedOutput = DataRecorder()
        let output = session.attachOutput { data in
            receivedOutput.append(data)
        }

        session.send(Data("whoami\r".utf8))
        transport.emit(Data("root\r\n".utf8))

        withExtendedLifetime(output) {
            XCTAssertEqual(transport.sentInput, [Data("whoami\r".utf8)])
            XCTAssertEqual(receivedOutput.values, [Data("root\r\n".utf8)])
        }
    }

    func testCancelledTerminalOutputStopsReceivingBytes() {
        let transport = RecordingTerminalTransport()
        let session = OrlixTerminalSession(transport: transport)
        let receivedOutput = DataRecorder()
        let output = session.attachOutput { data in
            receivedOutput.append(data)
        }

        output.cancel()
        transport.emit(Data("late output\r\n".utf8))

        XCTAssertTrue(receivedOutput.values.isEmpty)
    }

    private func repositoryRoot() throws -> URL {
        var url = URL(fileURLWithPath: #filePath)
        while url.path != "/" {
            let candidate = url.deletingLastPathComponent()
            if FileManager.default.fileExists(
                atPath: candidate.appendingPathComponent("project.yml").path
            ) {
                return candidate
            }
            url = candidate
        }
        throw NSError(
            domain: "OrlixOSTests",
            code: 1,
            userInfo: [NSLocalizedDescriptionKey: "could not locate repository root"]
        )
    }

    private func temporaryRegistryRoot() -> URL {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        addTeardownBlock {
            try? FileManager.default.removeItem(at: root)
        }
        return root
    }

    private struct TarFixtureEntry {
        var path: String
        var type: Unicode.Scalar = "0"
        var linkName: String = ""
        var payload: Data = Data()
        var sizeEncoding: TarNumericEncoding = .octal
        var uid: UInt64 = 0
        var gid: UInt64 = 0
        var devMajor: UInt64 = 0
        var devMinor: UInt64 = 0
        var numericEncoding: TarNumericEncoding = .octal
    }

    private enum TarNumericEncoding {
        case octal
        case base256
    }

    private func tarArchive(entries: [TarFixtureEntry]) -> Data {
        var data = Data()
        for entry in entries {
            var header = Data(repeating: 0, count: 512)
            write(entry.path, into: &header, at: 0, length: 100)
            writeNumeric(
                0o755,
                encoding: entry.numericEncoding,
                into: &header,
                at: 100,
                length: 8
            )
            writeNumeric(
                entry.uid,
                encoding: entry.numericEncoding,
                into: &header,
                at: 108,
                length: 8
            )
            writeNumeric(
                entry.gid,
                encoding: entry.numericEncoding,
                into: &header,
                at: 116,
                length: 8
            )
            writeNumeric(
                UInt64(entry.payload.count),
                encoding: entry.sizeEncoding,
                into: &header,
                at: 124,
                length: 12
            )
            writeOctal(0, into: &header, at: 136, length: 12)
            for index in 148..<156 {
                header[index] = UInt8(ascii: " ")
            }
            header[156] = UInt8(ascii: entry.type)
            write(entry.linkName, into: &header, at: 157, length: 100)
            write("ustar", into: &header, at: 257, length: 6)
            write("00", into: &header, at: 263, length: 2)
            writeNumeric(
                entry.devMajor,
                encoding: entry.numericEncoding,
                into: &header,
                at: 329,
                length: 8
            )
            writeNumeric(
                entry.devMinor,
                encoding: entry.numericEncoding,
                into: &header,
                at: 337,
                length: 8
            )
            let checksum = header.reduce(UInt64(0)) { $0 + UInt64($1) }
            writeOctal(checksum, into: &header, at: 148, length: 8)
            data.append(header)
            data.append(entry.payload)
            let remainder = entry.payload.count % 512
            if remainder != 0 {
                data.append(Data(repeating: 0, count: 512 - remainder))
            }
        }
        data.append(Data(repeating: 0, count: 1024))
        return data
    }

    private func paxExtendedHeaderPayload(_ fields: [String: String]) -> Data {
        Data(
            fields.keys.sorted().map { key in
                paxExtendedHeaderRecord(key: key, value: fields[key]!)
            }.joined().utf8
        )
    }

    private func paxExtendedHeaderRecord(key: String, value: String) -> String {
        let body = "\(key)=\(value)\n"
        var length = body.utf8.count + 2
        while true {
            let record = "\(length) \(body)"
            let actualLength = record.utf8.count
            if actualLength == length {
                return record
            }
            length = actualLength
        }
    }

    private func gnuLongNamePayload(_ value: String) -> Data {
        Data((value + "\0").utf8)
    }

    private func writeNumeric(
        _ value: UInt64,
        encoding: TarNumericEncoding,
        into data: inout Data,
        at offset: Int,
        length: Int
    ) {
        switch encoding {
        case .octal:
            writeOctal(value, into: &data, at: offset, length: length)
        case .base256:
            writeBase256(value, into: &data, at: offset, length: length)
        }
    }

    private func write(
        _ string: String,
        into data: inout Data,
        at offset: Int,
        length: Int
    ) {
        let bytes = Array(string.utf8.prefix(length))
        for index in 0..<bytes.count {
            data[offset + index] = bytes[index]
        }
    }

    private func writeOctal(
        _ value: UInt64,
        into data: inout Data,
        at offset: Int,
        length: Int
    ) {
        let text = String(value, radix: 8)
        let padded = String(repeating: "0", count: max(0, length - 2 - text.count))
            + text
            + "\0"
        write(padded, into: &data, at: offset, length: length)
    }

    private func writeBase256(
        _ value: UInt64,
        into data: inout Data,
        at offset: Int,
        length: Int
    ) {
        var value = value
        for index in stride(from: offset + length - 1, through: offset, by: -1) {
            data[index] = UInt8(value & 0xff)
            value >>= 8
        }
        data[offset] |= 0x80
    }

    private struct OCILayoutFixture {
        let root: URL
        let manifestDigest: String
        let configDigest: String
        let layerDigests: [String]
        let layerData: [Data]

        var layerDigest: String {
            layerDigests[0]
        }
    }

    private func writeOCILayout(
        layerData: [Data] = [Data("layer-bytes".utf8)],
        layerMediaTypes: [String]? = nil,
        manifestSizeOverride: Int? = nil,
        configSizeOverride: Int? = nil,
        layerSizeOverrides: [Int]? = nil,
        rootfsType: String? = nil,
        rootfsDiffIDs: [String]? = nil,
        manifestMediaType: String = "application/vnd.oci.image.manifest.v1+json",
        configMediaType: String = "application/vnd.oci.image.config.v1+json",
        platformVariant: String? = nil,
        user: String = "0",
        workingDirectory: String = "/",
        envEntries: [String] = ["PATH=/usr/bin:/bin", "EMPTY="],
        entrypoint: [String] = ["/bin/sh"],
        command: [String] = ["-c", "echo hello"]
    ) throws -> OCILayoutFixture {
        let root = temporaryRegistryRoot()
        let blobs = root.appendingPathComponent("blobs/sha256", isDirectory: true)
        try FileManager.default.createDirectory(
            at: blobs,
            withIntermediateDirectories: true
        )
        try #"{"imageLayoutVersion":"1.0.0"}"#
            .data(using: .utf8)!
            .write(to: root.appendingPathComponent("oci-layout"))

        let rootfsBlock: String
        if rootfsType != nil || rootfsDiffIDs != nil {
            let diffIDs = rootfsDiffIDs
                ?? layerData.map { "sha256:\(OrlixOCIDigest.sha256Hex($0))" }
            let encodedDiffIDs = diffIDs
                .map { #""\#($0)""# }
                .joined(separator: ", ")
            rootfsBlock = #"""
            ,
              "rootfs": {
                "type": "\#(rootfsType ?? "layers")",
                "diff_ids": [\#(encodedDiffIDs)]
              }
            """#
        } else {
            rootfsBlock = ""
        }
        let encodedEnvEntries = String(
            data: try JSONEncoder().encode(envEntries),
            encoding: .utf8
        )!
        let encodedEntrypoint = String(
            data: try JSONEncoder().encode(entrypoint),
            encoding: .utf8
        )!
        let encodedCommand = String(
            data: try JSONEncoder().encode(command),
            encoding: .utf8
        )!
        let configData = #"""
        {
          "config": {
            "Env": \#(encodedEnvEntries),
            "Entrypoint": \#(encodedEntrypoint),
            "Cmd": \#(encodedCommand),
            "WorkingDir": "\#(workingDirectory)",
            "User": "\#(user)"
          }
        \#(rootfsBlock)
        }
        """#.data(using: .utf8)!
        let configDigest = try writeOCIBlob(configData, under: blobs)
        let layerDigests = try layerData.map { try writeOCIBlob($0, under: blobs) }
        let mediaTypes = layerMediaTypes ?? Array(
            repeating: "application/vnd.oci.image.layer.v1.tar",
            count: layerData.count
        )
        let layerDescriptors = zip(zip(layerDigests, layerData), mediaTypes)
            .enumerated()
            .map { index, element in
                let (digestAndData, mediaType) = element
                let (digest, data) = digestAndData
                let advertisedSize = layerSizeOverrides?[index] ?? data.count
                return #"""
                    {
                      "mediaType": "\#(mediaType)",
                      "digest": "\#(digest)",
                      "size": \#(advertisedSize)
                    }
                """#
            }
            .joined(separator: ",\n")
        let advertisedConfigSize = configSizeOverride ?? configData.count
        let manifestData = #"""
        {
          "schemaVersion": 2,
          "config": {
            "mediaType": "\#(configMediaType)",
            "digest": "\#(configDigest)",
            "size": \#(advertisedConfigSize)
          },
          "layers": [
        \#(layerDescriptors)
          ]
        }
        """#.data(using: .utf8)!
        let manifestDigest = try writeOCIBlob(manifestData, under: blobs)
        let advertisedManifestSize = manifestSizeOverride ?? manifestData.count
        let platformVariantLine = platformVariant.map {
            ",\n                \"variant\": \"\($0)\""
        } ?? ""
        let indexData = #"""
        {
          "schemaVersion": 2,
          "manifests": [
            {
              "mediaType": "\#(manifestMediaType)",
              "digest": "\#(manifestDigest)",
              "size": \#(advertisedManifestSize),
              "platform": {
                "os": "linux",
                "architecture": "arm64"\#(platformVariantLine)
              }
            }
          ]
        }
        """#.data(using: .utf8)!
        try indexData.write(to: root.appendingPathComponent("index.json"))

        return OCILayoutFixture(
            root: root,
            manifestDigest: manifestDigest,
            configDigest: configDigest,
            layerDigests: layerDigests,
            layerData: layerData
        )
    }

    private func writeOCIBlob(_ data: Data, under blobs: URL) throws -> String {
        let hex = OrlixOCIDigest.sha256Hex(data)
        try data.write(to: blobs.appendingPathComponent(hex))
        return "sha256:\(hex)"
    }

    private func gzip(_ data: Data) throws -> Data {
        var stream = z_stream()
        let initStatus = deflateInit2_(
            &stream,
            Z_DEFAULT_COMPRESSION,
            Z_DEFLATED,
            MAX_WBITS + 16,
            8,
            Z_DEFAULT_STRATEGY,
            ZLIB_VERSION,
            Int32(MemoryLayout<z_stream>.size)
        )
        XCTAssertEqual(initStatus, Z_OK)
        guard initStatus == Z_OK else {
            throw NSError(domain: "OrlixOSTests.gzip", code: Int(initStatus))
        }
        defer {
            deflateEnd(&stream)
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
                    return deflate(&stream, Z_FINISH)
                }
                let produced = chunkSize - Int(stream.avail_out)
                if produced > 0 {
                    output.append(chunk, count: produced)
                }
                if status == Z_STREAM_END {
                    break
                }
                if status != Z_OK {
                    return status
                }
                if produced == 0 {
                    return Z_BUF_ERROR
                }
            }
            return status
        }
        XCTAssertEqual(status, Z_STREAM_END)
        guard status == Z_STREAM_END else {
            throw NSError(domain: "OrlixOSTests.gzip", code: Int(status))
        }
        return output
    }
}

private final class DataRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var storage: [Data] = []

    var values: [Data] {
        lock.lock()
        defer { lock.unlock() }
        return storage
    }

    func append(_ data: Data) {
        lock.lock()
        storage.append(data)
        lock.unlock()
    }
}

private final class RecordingTerminalTransport:
    OrlixTerminalTransport,
    @unchecked Sendable
{
    private var outputHandlers: [UUID: @Sendable (Data) -> Void] = [:]
    private(set) var sentInput: [Data] = []

    func attachOutput(
        _ handler: @escaping @Sendable (Data) -> Void
    ) -> OrlixTerminalOutput {
        let id = UUID()
        outputHandlers[id] = handler
        return OrlixTerminalOutput { [weak self] in
            self?.outputHandlers[id] = nil
        }
    }

    func send(_ data: Data) {
        sentInput.append(data)
    }

    func emit(_ data: Data) {
        for handler in outputHandlers.values {
            handler(data)
        }
    }
}
