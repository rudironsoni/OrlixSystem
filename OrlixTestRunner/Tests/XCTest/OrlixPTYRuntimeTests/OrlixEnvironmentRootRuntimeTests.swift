@_spi(OrlixPrivateTesting) @testable import OrlixOS
import CryptoKit
import Foundation
import XCTest

final class OrlixEnvironmentRootRuntimeTests: XCTestCase {
    func testTarDerivedMaterializedRootBootsAndExposesOSRelease() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .tarDerived
        )
        let output = try runner.run()

        XCTAssertTrue(output.contains("ORLIX_ENV_OS_RELEASE_BEGIN"))
        XCTAssertTrue(output.contains("ID=orlix-tar-runtime-proof"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OS_RELEASE_DONE"))
    }

    func testTarDerivedMaterializedRootExposesLinuxPseudoFilesystems()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .tarDerived,
            proof: .pseudoFilesystems
        )
        let output = try runner.run()

        XCTAssertTrue(output.contains("ORLIX_ENV_PSEUDOFS_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PROC_MOUNTS_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PROC_SELF_STATUS_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PROC_SELF_FD_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_NULL_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_URANDOM_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_PTMX_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_PTS_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_SYS_BLOCK_VDA_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_SYS_BLOCK_VDB_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PSEUDOFS_DONE"))
    }

    func testTarDerivedMaterializedRootUsesLinuxRuntimeTmpfsMounts()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .tarDerived,
            proof: .runtimeTmpfs
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_TMPFS_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_TMP_MOUNT_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_RUN_MOUNT_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_SHM_MOUNT_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_TMP_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_RUN_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_SHM_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_TMPFS_DONE"))
    }

    func testCopiedNamedEnvironmentMaterializedRootBootsAndExposesOSRelease()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .tarDerived
        )
        let output = try runner.runCopiedNamedEnvironment()

        XCTAssertTrue(output.contains("ORLIX_ENV_OS_RELEASE_BEGIN"))
        XCTAssertTrue(output.contains("ID=orlix-tar-runtime-proof"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OS_RELEASE_DONE"))
    }

    func testCopiedNamedEnvironmentOverlayMutationDoesNotChangeParent()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .tarDerived,
            proof: .overlayMutation
        )
        let output = try runner
            .runCopiedNamedEnvironmentValidatingParentIsolation()

        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_MUTATION_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_COPYUP_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_COPYUP_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_UNLINK_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_MUTATION_DONE"))
    }

    func testCopiedNamedEnvironmentSessionSelectionEntersRootAndDescriptor()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .descriptorExecution
        )
        let output = try runner.runCopiedNamedEnvironmentThroughSessionSelection()

        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_BEGIN"))
        XCTAssertTrue(output.contains("argv0=orlix-descriptor-xxxx"))
        XCTAssertTrue(output.contains("argv1=argument with spaces"))
        XCTAssertTrue(output.contains("env=descriptor value with spaces"))
        XCTAssertTrue(output.contains("pwd=/tmp"))
        XCTAssertTrue(output.contains("Uid:\t1000"))
        XCTAssertTrue(output.contains("Gid:\t100"))
        XCTAssertTrue(output.contains("ID=orlix-oci-runtime-proof"))
        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_DONE"))
    }

    func testTarDerivedNamedEnvironmentCrossBootWrite() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .tarDerived,
            proof: .crossBootWrite
        )
        let output = try runner.runPersistentCopiedNamedEnvironmentCrossBootWrite()

        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_WRITE_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_SYNC_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_REREAD_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_WRITE_DONE"))
    }

    func testTarDerivedNamedEnvironmentCrossBootVerify() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .tarDerived,
            proof: .crossBootVerify
        )
        let output = try runner.runPersistentCopiedNamedEnvironmentCrossBootVerify()

        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_VERIFY_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_SURVIVED_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_CLEANUP_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_VERIFY_DONE"))
    }

    func testOCIDerivedNamedEnvironmentCrossBootWrite() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .crossBootWrite
        )
        let output = try runner.runPersistentCopiedNamedEnvironmentCrossBootWrite()

        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_WRITE_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_SYNC_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_REREAD_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_WRITE_DONE"))
    }

    func testOCIDerivedNamedEnvironmentCrossBootVerify() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .crossBootVerify
        )
        let output = try runner.runPersistentCopiedNamedEnvironmentCrossBootVerify()

        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_VERIFY_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_SURVIVED_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_CLEANUP_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_CROSSBOOT_VERIFY_DONE"))
    }

    func testOCIDerivedMaterializedRootBootsAndExposesOSRelease() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived
        )
        let output = try runner.run()

        XCTAssertTrue(output.contains("ORLIX_ENV_OS_RELEASE_BEGIN"))
        XCTAssertTrue(output.contains("ID=orlix-oci-runtime-proof"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OS_RELEASE_DONE"))
    }

    func testOCIDerivedMaterializedRootUsesLinuxOverlayCopyUpAndWhiteout() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .overlayMutation
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_MUTATION_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_COPYUP_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_COPYUP_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_UNLINK_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_MUTATION_DONE"))
    }

    func testOCIDerivedOverlayMutationLeavesBaseImageStableAndChangesStateImage()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .overlayMutation
        )
        let output = try runner.runOnMutableFixtureCopyValidatingOverlayStorage()

        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_MUTATION_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_COPYUP_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_COPYUP_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_UNLINK_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_OVERLAY_MUTATION_DONE"))
    }

    func testOCIDerivedMaterializedRootBindsDescriptorExecutionDefaults()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .descriptorExecution
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_BEGIN"))
        XCTAssertTrue(output.contains("argv0=orlix-descriptor-xxxx"))
        XCTAssertTrue(output.contains("argv1=argument with spaces"))
        XCTAssertTrue(output.contains("env=descriptor value with spaces"))
        XCTAssertTrue(output.contains("pwd=/tmp"))
        XCTAssertTrue(output.contains("Uid:\t1000"))
        XCTAssertTrue(output.contains("Gid:\t100"))
        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_DONE"))
    }

    func testOCIDerivedMaterializedRootBindsLongDescriptorExecutionDefaults()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .longDescriptorExecution
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_BEGIN"))
        XCTAssertTrue(output.contains("argv0=orlix-descriptor-xxxxxxxx"))
        XCTAssertTrue(output.contains("argv1=argument with spaces"))
        XCTAssertTrue(output.contains("env=descriptor value with spaces"))
        XCTAssertTrue(output.contains("pwd=/tmp"))
        XCTAssertTrue(output.contains("Uid:\t1000"))
        XCTAssertTrue(output.contains("Gid:\t100"))
        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_DONE"))
    }

    func testOCIDerivedMaterializedRootRunsLinuxPathDescriptorDefaults()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .linuxPathDescriptorExecution
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_BEGIN"))
        XCTAssertTrue(output.contains("argv0=linux-path-argv0"))
        XCTAssertTrue(output.contains("argv1="))
        XCTAssertTrue(output.contains("argv2=argument after empty"))
        XCTAssertTrue(output.contains("pwd=/"))
        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_DONE"))
    }

    func testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughPath()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .pathLookupDescriptorExecution
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_BEGIN"))
        XCTAssertTrue(output.contains("argv0=path-lookup-argv0"))
        XCTAssertTrue(output.contains("argv1=argument after path lookup"))
        XCTAssertTrue(output.contains("pwd=/"))
        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_DONE"))
    }

    func testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughFallbackPath()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .pathLookupWithoutPATHDescriptorExecution
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_BEGIN"))
        XCTAssertTrue(output.contains("argv0=path-fallback-argv0"))
        XCTAssertTrue(output.contains("argv1=argument after fallback path lookup"))
        XCTAssertTrue(output.contains("env=descriptor value without path"))
        XCTAssertTrue(output.contains("pwd=/"))
        XCTAssertTrue(output.contains("ORLIX_ENV_EXEC_DONE"))
    }

    func testOCIDerivedMaterializedRootExposesLinuxPseudoFilesystems()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .pseudoFilesystems
        )
        let output = try runner.run()

        XCTAssertTrue(output.contains("ORLIX_ENV_PSEUDOFS_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PROC_MOUNTS_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PROC_SELF_STATUS_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PROC_SELF_FD_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_NULL_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_URANDOM_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_PTMX_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_PTS_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_SYS_BLOCK_VDA_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_SYS_BLOCK_VDB_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PSEUDOFS_DONE"))
    }

    func testOCIDerivedMaterializedRootUsesLinuxPTYStdio() throws {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .ptyStdio
        )
        let output = try runner.run()

        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_STDIN_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_STDOUT_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_STDERR_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_PATH_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_WAITING_FOR_INPUT"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_DELAYED_INPUT_OK"))
        XCTAssertTrue(output.contains("/dev/pts/"))
        XCTAssertTrue(output.contains("ORLIX_ENV_PTY_DONE"))
    }

    func testOCIDerivedMaterializedRootUsesLinuxRuntimeTmpfsMounts()
        throws
    {
        let runner = OrlixEnvironmentRootRuntimeProofRunner(
            fixture: .ociDerived,
            proof: .runtimeTmpfs
        )
        let output = try runner.runOnMutableFixtureCopy()

        XCTAssertTrue(output.contains("ORLIX_ENV_TMPFS_BEGIN"))
        XCTAssertTrue(output.contains("ORLIX_ENV_TMP_MOUNT_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_RUN_MOUNT_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_SHM_MOUNT_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_TMP_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_RUN_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_DEV_SHM_WRITE_OK"))
        XCTAssertTrue(output.contains("ORLIX_ENV_TMPFS_DONE"))
    }
}

private final class OrlixEnvironmentRootRuntimeProofRunner: @unchecked Sendable {
    private static let readyFile = ".ready"
    private static let firstOutputTimeout: TimeInterval = 30
    private static let timeout: TimeInterval = 600
    fileprivate static let osReleaseCommandScript = [
        #"printf '%s%s\n' ORLIX_ENV_ OS_RELEASE_BEGIN"#,
        #"/bin/cat /etc/os-release"#,
        #"printf '%s%s\n' ORLIX_ENV_ OS_RELEASE_DONE"#,
    ].joined(separator: "\r") + "\r"
    private let fixture: RuntimeFixture
    private let proof: RuntimeProof

    init(
        fixture: RuntimeFixture,
        proof: RuntimeProof = .osRelease
    ) {
        self.fixture = fixture
        self.proof = proof
    }

    func run() throws -> String {
        let fixtureRoot = try Self.fixtureRoot(for: fixture)
        return try run(fixtureRoot: fixtureRoot)
    }

    func runOnMutableFixtureCopy() throws -> String {
        let sourceRoot = try Self.fixtureRoot(for: fixture)
        let copiedRoot = try Self.mutableFixtureCopy(of: sourceRoot, fixture: fixture)
        defer {
            try? FileManager.default.removeItem(at: copiedRoot.root)
        }

        return try run(fixtureRoot: copiedRoot)
    }

    func runOnMutableFixtureCopyValidatingOverlayStorage() throws -> String {
        let sourceRoot = try Self.fixtureRoot(for: fixture)
        let copiedRoot = try Self.mutableFixtureCopy(of: sourceRoot, fixture: fixture)
        defer {
            try? FileManager.default.removeItem(at: copiedRoot.root)
        }

        let descriptor = descriptor(
            environmentID: fixture.environmentID,
            source: fixture.source,
            rootImageIdentifier: fixture.rootImageIdentifier
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: copiedRoot.linuxStateRoot,
            cacheRoot: copiedRoot.cacheRoot,
            scratchRoot: copiedRoot.scratchRoot
        )
        let baseBefore = try Self.sha256Hex(of: layout.baseImageURL)
        let stateBefore = try Self.sha256Hex(of: layout.stateImageURL)

        let output = try run(fixtureRoot: copiedRoot, descriptor: descriptor)

        XCTAssertEqual(
            try Self.sha256Hex(of: layout.baseImageURL),
            baseBefore,
            "Overlay mutation must not rewrite the read-only lower base image."
        )
        XCTAssertNotEqual(
            try Self.sha256Hex(of: layout.stateImageURL),
            stateBefore,
            "Overlay mutation must persist into the writable state image."
        )
        return output
    }

    func runCopiedNamedEnvironment() throws -> String {
        let sourceRoot = try Self.fixtureRoot(for: fixture)
        let copiedRoot = try Self.mutableFixtureCopy(of: sourceRoot, fixture: fixture)
        defer {
            try? FileManager.default.removeItem(at: copiedRoot.root)
        }

        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: copiedRoot.linuxStateRoot,
            cacheRoot: copiedRoot.cacheRoot,
            scratchRoot: copiedRoot.scratchRoot
        )
        let parentDescriptor = descriptor(
            environmentID: fixture.environmentID,
            source: fixture.source,
            rootImageIdentifier: fixture.rootImageIdentifier
        )
        try registry.save(parentDescriptor)
        let copiedDescriptor = try registry.copyEnvironment(
            from: fixture.environmentID,
            to: fixture.copiedEnvironmentID,
            rootImageIdentifier: fixture.copiedRootImageIdentifier
        )

        return try run(fixtureRoot: copiedRoot, descriptor: copiedDescriptor)
    }

    func runCopiedNamedEnvironmentValidatingParentIsolation() throws -> String {
        let sourceRoot = try Self.fixtureRoot(for: fixture)
        let copiedRoot = try Self.mutableFixtureCopy(of: sourceRoot, fixture: fixture)
        defer {
            try? FileManager.default.removeItem(at: copiedRoot.root)
        }

        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: copiedRoot.linuxStateRoot,
            cacheRoot: copiedRoot.cacheRoot,
            scratchRoot: copiedRoot.scratchRoot
        )
        let parentDescriptor = descriptor(
            environmentID: fixture.environmentID,
            source: fixture.source,
            rootImageIdentifier: fixture.rootImageIdentifier
        )
        try registry.save(parentDescriptor)
        let parentLayout = try registry.layout(forEnvironmentID: fixture.environmentID)
        let parentBaseBefore = try Self.sha256Hex(of: parentLayout.baseImageURL)
        let parentStateBefore = try Self.sha256Hex(of: parentLayout.stateImageURL)

        let copiedDescriptor = try registry.copyEnvironment(
            from: fixture.environmentID,
            to: fixture.copiedEnvironmentID,
            rootImageIdentifier: fixture.copiedRootImageIdentifier
        )
        let copiedLayout = try registry.layout(
            forEnvironmentID: fixture.copiedEnvironmentID
        )
        let copiedBaseBefore = try Self.sha256Hex(of: copiedLayout.baseImageURL)
        let copiedStateBefore = try Self.sha256Hex(of: copiedLayout.stateImageURL)

        let output = try run(fixtureRoot: copiedRoot, descriptor: copiedDescriptor)

        XCTAssertEqual(
            try Self.sha256Hex(of: parentLayout.baseImageURL),
            parentBaseBefore,
            "Copied environment mutation must not rewrite the parent base image."
        )
        XCTAssertEqual(
            try Self.sha256Hex(of: parentLayout.stateImageURL),
            parentStateBefore,
            "Copied environment mutation must not rewrite the parent state image."
        )
        XCTAssertEqual(
            try Self.sha256Hex(of: copiedLayout.baseImageURL),
            copiedBaseBefore,
            "Copied environment mutation must not rewrite its read-only base image."
        )
        XCTAssertNotEqual(
            try Self.sha256Hex(of: copiedLayout.stateImageURL),
            copiedStateBefore,
            "Copied environment mutation must persist into the copied writable state image."
        )
        return output
    }

    func runCopiedNamedEnvironmentThroughSessionSelection() throws -> String {
        let sourceRoot = try Self.fixtureRoot(for: fixture)
        let copiedRoot = try Self.mutableFixtureCopy(of: sourceRoot, fixture: fixture)
        defer {
            try? FileManager.default.removeItem(at: copiedRoot.root)
        }

        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: copiedRoot.linuxStateRoot,
            cacheRoot: copiedRoot.cacheRoot,
            scratchRoot: copiedRoot.scratchRoot
        )
        let parentDescriptor = descriptor(
            environmentID: fixture.environmentID,
            source: fixture.source,
            rootImageIdentifier: fixture.rootImageIdentifier
        )
        try registry.save(parentDescriptor)
        let copiedDescriptor = try registry.copyEnvironment(
            from: fixture.environmentID,
            to: fixture.copiedEnvironmentID,
            rootImageIdentifier: fixture.copiedRootImageIdentifier
        )

        return try run(
            fixtureRoot: copiedRoot,
            descriptor: copiedDescriptor,
            launchThroughSessionSelection: true
        )
    }

    func runPersistentCopiedNamedEnvironmentCrossBootWrite() throws -> String {
        let fixtureRoot = try Self.preparePersistentCrossBootFixtureCopy(
            source: Self.fixtureRoot(for: fixture),
            fixture: fixture
        )
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: fixtureRoot.linuxStateRoot,
            cacheRoot: fixtureRoot.cacheRoot,
            scratchRoot: fixtureRoot.scratchRoot
        )
        let parentDescriptor = descriptor(
            environmentID: fixture.environmentID,
            source: fixture.source,
            rootImageIdentifier: fixture.rootImageIdentifier
        )
        try registry.save(parentDescriptor)
        let copiedDescriptor = try registry.copyEnvironment(
            from: fixture.environmentID,
            to: fixture.copiedEnvironmentID,
            rootImageIdentifier: fixture.copiedRootImageIdentifier
        )

        return try run(
            fixtureRoot: fixtureRoot,
            descriptor: copiedDescriptor,
            launchThroughSessionSelection: true
        )
    }

    func runPersistentCopiedNamedEnvironmentCrossBootVerify() throws -> String {
        let fixtureRoot = try Self.persistentCrossBootFixtureCopy(for: fixture)
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: fixtureRoot.linuxStateRoot,
            cacheRoot: fixtureRoot.cacheRoot,
            scratchRoot: fixtureRoot.scratchRoot
        )
        let descriptor = try registry.load(environmentID: fixture.copiedEnvironmentID)

        return try run(
            fixtureRoot: fixtureRoot,
            descriptor: descriptor,
            launchThroughSessionSelection: true
        )
    }

    private func run(fixtureRoot: EnvironmentRootFixture) throws -> String {
        try run(
            fixtureRoot: fixtureRoot,
            descriptor: descriptor(
                environmentID: fixture.environmentID,
                source: fixture.source,
                rootImageIdentifier: fixture.rootImageIdentifier
            )
        )
    }

    private func run(
        fixtureRoot: EnvironmentRootFixture,
        descriptor: OrlixEnvironmentDescriptor,
        launchThroughSessionSelection: Bool = false
    ) throws -> String {
        let registry = OrlixEnvironmentRegistry(
            linuxStateRoot: fixtureRoot.linuxStateRoot,
            cacheRoot: fixtureRoot.cacheRoot,
            scratchRoot: fixtureRoot.scratchRoot
        )
        let layout = try OrlixEnvironmentStorageLayout.layout(
            forEnvironmentID: descriptor.id,
            linuxStateRoot: fixtureRoot.linuxStateRoot,
            cacheRoot: fixtureRoot.cacheRoot,
            scratchRoot: fixtureRoot.scratchRoot
        )
        let terminal = OrlixTerminalSession()
        let session: OrlixLinuxSession
        if launchThroughSessionSelection {
            session = try OrlixLinuxSession(
                environmentID: descriptor.id,
                registry: registry,
                terminal: terminal
            )
        } else {
            let rootImage = try OrlixEnvironmentRootImage.materialized(
                descriptor: descriptor,
                layout: layout
            )
            session = OrlixLinuxSession(
                materializedRootImage: rootImage,
                terminal: terminal
            )
        }
        let terminalLog = EnvironmentRootTerminalLog()
        terminalLog.writeLine("fixture=\(fixtureRoot.root.path)")
        terminalLog.writeLine("base=\(layout.baseImageURL.path)")
        terminalLog.writeLine("state=\(layout.stateImageURL.path)")
        terminalLog.writeLine(
            "rootImageIdentifier=\(session.bootConfig.rootImageIdentifier)"
        )

        let recorder = EnvironmentRootOutputRecorder(terminalLog: terminalLog)
        let bootStatus = EnvironmentRootBootStatusRecorder()
        let completion = DispatchSemaphore(value: 0)
        let output = session.terminal.attachOutput { data in
            recorder.append(data)
            let text = recorder.text

            if self.proof.sendsInteractiveCommands,
               !recorder.hasSentProofCommands,
               Self.containsShellPrompt(text) {
                terminalLog.writeLine("shell prompt detected; sending environment proof commands")
                recorder.markProofCommandsSent()
                session.terminal.send(Data(self.proof.commandScript.utf8))
            }

            if let delayedInput = self.proof.delayedInput,
               !recorder.hasSentDelayedInput,
               text.contains(self.proof.delayedInputPrompt) {
                terminalLog.writeLine("delayed PTY input prompt detected; sending payload")
                recorder.markDelayedInputSent()
                session.terminal.send(Data(delayedInput.utf8))
            }

            if let postDelayedInputScript = self.proof.postDelayedInputScript,
               !recorder.hasSentPostDelayedInputScript,
               text.contains(self.proof.delayedInputSuccessMarker) {
                terminalLog.writeLine("delayed PTY input accepted; sending completion command")
                recorder.markPostDelayedInputScriptSent()
                session.terminal.send(Data(postDelayedInputScript.utf8))
            }

            if Self.containsTerminalCondition(text, proof: self.proof) {
                completion.signal()
            }
        }
        defer { output.cancel() }

        DispatchQueue.global(qos: .userInitiated).async {
            terminalLog.writeLine("boot starting")
            let status = session.boot()
            terminalLog.writeLine("boot returned status=\(status.message)")
            bootStatus.set(status)
            if status != .ok {
                completion.signal()
            }
        }

        let deadline = Date().addingTimeInterval(Self.timeout)
        let firstOutputDeadline = Date().addingTimeInterval(Self.firstOutputTimeout)
        while completion.wait(timeout: .now() + .seconds(1)) != .success {
            if recorder.byteCount == 0,
               Date() >= firstOutputDeadline {
                throw OrlixEnvironmentRootRuntimeProofError.noTerminalOutput(
                    Self.firstOutputTimeout,
                    terminalLog.url
                )
            }
            if Date() >= deadline {
                let text = recorder.text
                if let marker = Self.firstFatalMarker(in: text) {
                    throw OrlixEnvironmentRootRuntimeProofError.fatalMarker(marker)
                }
                throw OrlixEnvironmentRootRuntimeProofError.timeout(
                    Self.timeout,
                    text,
                    terminalLog.url,
                    terminalLog.tail()
                )
            }
        }

        if let status = bootStatus.value, status != .ok {
            if status == .alreadyStarted {
                throw XCTSkip(
                    "OrlixBoot is one boot per XCTest app process; run this runtime proof as a focused test for Linux execution evidence."
                )
            }
            throw OrlixEnvironmentRootRuntimeProofError.bootFailed(status)
        }

        let text = Self.normalized(recorder.text)
        try validate(text, terminalLog: terminalLog)
        return text
    }

    private func descriptor(
        environmentID: String,
        source: OrlixEnvironmentSource,
        rootImageIdentifier: String
    ) -> OrlixEnvironmentDescriptor {
        OrlixEnvironmentDescriptor(
            id: environmentID,
            source: source,
            platform: "linux/arm64",
            rootImageIdentifier: rootImageIdentifier,
            defaultCommand: proof.defaultCommand,
            defaultEnvironment: proof.defaultEnvironment,
            defaultWorkingDirectory: proof.defaultWorkingDirectory,
            defaultUserID: proof.defaultUserID,
            defaultGroupID: proof.defaultGroupID
        )
    }

    private static func mutableFixtureCopy(
        of sourceRoot: EnvironmentRootFixture,
        fixture: RuntimeFixture
    ) throws -> EnvironmentRootFixture {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent(
                "orlix-\(fixture.directoryName)-\(UUID().uuidString)",
                isDirectory: true
            )
        try FileManager.default.copyItem(at: sourceRoot.root, to: root)
        return EnvironmentRootFixture(root: root)
    }

    private static func persistentCrossBootFixtureCopy(
        for fixture: RuntimeFixture
    ) throws -> EnvironmentRootFixture {
        let root = try persistentCrossBootFixtureRoot(for: fixture)
        guard FileManager.default.fileExists(atPath: root.path) else {
            throw XCTSkip(
                "missing persistent \(fixture.description) cross-boot fixture: \(root.path)"
            )
        }
        return EnvironmentRootFixture(root: root)
    }

    private static func preparePersistentCrossBootFixtureCopy(
        source sourceRoot: EnvironmentRootFixture,
        fixture: RuntimeFixture
    ) throws -> EnvironmentRootFixture {
        let root = try persistentCrossBootFixtureRoot(for: fixture)
        if FileManager.default.fileExists(atPath: root.path) {
            try FileManager.default.removeItem(at: root)
        }
        try FileManager.default.createDirectory(
            at: root.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try FileManager.default.copyItem(at: sourceRoot.root, to: root)
        return EnvironmentRootFixture(root: root)
    }

    private static func persistentCrossBootFixtureRoot(
        for fixture: RuntimeFixture
    ) throws -> URL {
        try FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        )
        .appendingPathComponent("OrlixEnvironmentCrossBoot", isDirectory: true)
        .appendingPathComponent(fixture.directoryName, isDirectory: true)
    }

    private static func fixtureRoot(
        for fixture: RuntimeFixture
    ) throws -> EnvironmentRootFixture {
        let repoRoot = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
        let root = repoRoot
            .appendingPathComponent("Build", isDirectory: true)
            .appendingPathComponent("OrlixOS", isDirectory: true)
            .appendingPathComponent("environment-runtime-proof", isDirectory: true)
            .appendingPathComponent(fixture.directoryName, isDirectory: true)
        let marker = root.appendingPathComponent(readyFile, isDirectory: false)

        guard FileManager.default.fileExists(atPath: marker.path) else {
            throw XCTSkip(
                "missing \(fixture.description) materialized root fixture: \(marker.path)"
            )
        }
        try verifyFixtureReadyFile(
            marker: marker,
            fixture: fixture,
            repoRoot: repoRoot
        )

        return EnvironmentRootFixture(root: root)
    }

    private static func verifyFixtureReadyFile(
        marker: URL,
        fixture: RuntimeFixture,
        repoRoot: URL
    ) throws {
        let ready = try parseReadyFile(marker)

        guard ready["profile"] == "release" else {
            throw OrlixEnvironmentRootRuntimeProofError.staleFixture(
                "fixture \(fixture.description) was not generated for release profile"
            )
        }
        guard ready["fixture"] == fixture.directoryName else {
            throw OrlixEnvironmentRootRuntimeProofError.staleFixture(
                "fixture marker does not match \(fixture.directoryName)"
            )
        }
        guard ready["environment"] == fixture.environmentID else {
            throw OrlixEnvironmentRootRuntimeProofError.staleFixture(
                "fixture marker does not match \(fixture.environmentID)"
            )
        }

        let initURL = repoRoot
            .appendingPathComponent("Build", isDirectory: true)
            .appendingPathComponent("OrlixOS", isDirectory: true)
            .appendingPathComponent("packages", isDirectory: true)
            .appendingPathComponent("release", isDirectory: true)
            .appendingPathComponent("sbin", isDirectory: true)
            .appendingPathComponent("init", isDirectory: false)
        guard FileManager.default.fileExists(atPath: initURL.path) else {
            throw OrlixEnvironmentRootRuntimeProofError.staleFixture(
                "missing packaged init used to verify fixture freshness: \(initURL.path)"
            )
        }

        let expectedHash = try sha256Hex(of: initURL)
        guard ready["init_sha256"] == expectedHash else {
            throw OrlixEnvironmentRootRuntimeProofError.staleFixture(
                "fixture \(fixture.description) was generated from a stale init"
            )
        }
    }

    private static func parseReadyFile(_ marker: URL) throws -> [String: String] {
        let contents = try String(contentsOf: marker, encoding: .utf8)
        return contents.split(separator: "\n").reduce(into: [:]) { result, line in
            let parts = line.split(separator: "=", maxSplits: 1)
            guard parts.count == 2 else {
                return
            }
            result[String(parts[0])] = String(parts[1])
        }
    }

    private static func sha256Hex(of url: URL) throws -> String {
        let digest = SHA256.hash(data: try Data(contentsOf: url))
        return digest.map { String(format: "%02x", $0) }.joined()
    }

    private static func containsTerminalCondition(
        _ rawOutput: String,
        proof: RuntimeProof
    ) -> Bool {
        let output = normalized(rawOutput)

        return output.contains(proof.doneMarker) ||
            firstFatalMarker(in: output) != nil
    }

    private func validate(
        _ output: String,
        terminalLog: EnvironmentRootTerminalLog
    ) throws {
        if let marker = firstFatalMarker(in: output) {
            throw OrlixEnvironmentRootRuntimeProofError.fatalMarker(marker)
        }
        for marker in proof.requiredMarkers(for: fixture) {
            guard output.contains(marker) else {
                throw OrlixEnvironmentRootRuntimeProofError.missingMarker(
                    marker,
                    terminalLog.url
                )
            }
        }
    }

    private static func containsShellPrompt(_ rawOutput: String) -> Bool {
        let tail = String(normalized(rawOutput).suffix(4096))

        return tail.contains("sh-5.3# ") ||
            tail.range(
                of: #"[A-Za-z_-]*sh-[0-9][^\n#]*# "#,
                options: .regularExpression
            ) != nil
    }

    private static func normalized(_ text: String) -> String {
        stripANSI(
            text.replacingOccurrences(of: "\r\n", with: "\n")
                .replacingOccurrences(of: "\r", with: "\n")
        )
    }

    private static func stripANSI(_ text: String) -> String {
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
                if sequenceScalar.value >= 0x40 &&
                    sequenceScalar.value <= 0x7e {
                    break
                }
            }
        }

        return output
    }

    private static func firstFatalMarker(in output: String) -> String? {
        [
            "Kernel panic",
            "kernel panic",
            "panic:",
            "Out of memory",
            "oom-kill",
            "Killed process",
            "orlix-init: open PTY master failed",
            "orlix-init: unlock PTY failed",
            "orlix-init: get PTY number failed",
            "orlix-init: shell TIOCSCTTY failed",
            "orlix-init: PTY shell session ended",
            "VFS: Cannot open root device",
            "No working init found",
            "ORLIX_ENV_OVERLAY_PROOF_FAILED_BASE_READ",
            "ORLIX_ENV_OVERLAY_PROOF_FAILED_COPYUP_WRITE",
            "ORLIX_ENV_OVERLAY_PROOF_FAILED_COPYUP_READ",
            "ORLIX_ENV_OVERLAY_PROOF_FAILED_UNLINK",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_PROC_DIR",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_PROC_MOUNTS",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_PROC_SELF_STATUS",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_PROC_SELF_FD",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_DEV_DIR",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_DEV_NULL",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_DEV_URANDOM",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_DEV_TTY",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_DEV_PTMX",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_DEV_PTS",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_SYS_DIR",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_SYS_BLOCK_VDA",
            "ORLIX_ENV_PSEUDOFS_PROOF_FAILED_SYS_BLOCK_VDB",
            "ORLIX_ENV_PTY_PROOF_FAILED_STDIN",
            "ORLIX_ENV_PTY_PROOF_FAILED_STDOUT",
            "ORLIX_ENV_PTY_PROOF_FAILED_STDERR",
            "ORLIX_ENV_PTY_PROOF_FAILED_TTY_COMMAND",
            "ORLIX_ENV_PTY_PROOF_FAILED_PATH",
            "ORLIX_ENV_TMPFS_PROOF_FAILED_TMP_MOUNT",
            "ORLIX_ENV_TMPFS_PROOF_FAILED_RUN_MOUNT",
            "ORLIX_ENV_TMPFS_PROOF_FAILED_DEV_SHM_MOUNT",
            "ORLIX_ENV_TMPFS_PROOF_FAILED_TMP_WRITE",
            "ORLIX_ENV_TMPFS_PROOF_FAILED_RUN_WRITE",
            "ORLIX_ENV_TMPFS_PROOF_FAILED_DEV_SHM_WRITE",
            "ORLIX_ENV_CROSSBOOT_PROOF_FAILED_WRITE",
            "ORLIX_ENV_CROSSBOOT_PROOF_FAILED_SYNC",
            "ORLIX_ENV_CROSSBOOT_PROOF_FAILED_REREAD",
            "ORLIX_ENV_CROSSBOOT_PROOF_FAILED_VERIFY",
            "ORLIX_ENV_CROSSBOOT_PROOF_FAILED_CLEANUP",
        ].first { output.contains($0) }
    }

    private func firstFatalMarker(in output: String) -> String? {
        let oneShotCompleted = !proof.sendsInteractiveCommands &&
            output.contains(proof.doneMarker)
        let marker = Self.firstFatalMarker(in: output)

        if oneShotCompleted &&
            marker == "orlix-init: PTY shell session ended" {
            return nil
        }
        return marker
    }
}

private enum RuntimeProof: Sendable {
    case osRelease
    case overlayMutation
    case descriptorExecution
    case longDescriptorExecution
    case linuxPathDescriptorExecution
    case pathLookupDescriptorExecution
    case pathLookupWithoutPATHDescriptorExecution
    case pseudoFilesystems
    case ptyStdio
    case runtimeTmpfs
    case crossBootWrite
    case crossBootVerify

    var defaultCommand: [String] {
        switch self {
        case .osRelease, .overlayMutation, .pseudoFilesystems, .ptyStdio,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify:
            return ["/bin/sh"]
        case .descriptorExecution, .longDescriptorExecution:
            return [
                "/bin/sh",
                "-c",
                descriptorExecutionScript,
                descriptorExecutionArgument0,
                "argument with spaces"
            ]
        case .linuxPathDescriptorExecution:
            return [
                "/bin/../bin/sh",
                "-c",
                descriptorExecutionScript,
                "linux-path-argv0",
                "",
                "argument after empty"
            ]
        case .pathLookupDescriptorExecution:
            return [
                "sh",
                "-c",
                descriptorExecutionScript,
                "path-lookup-argv0",
                "argument after path lookup"
            ]
        case .pathLookupWithoutPATHDescriptorExecution:
            return [
                "sh",
                "-c",
                descriptorExecutionScript,
                "path-fallback-argv0",
                "argument after fallback path lookup"
            ]
        }
    }

    var defaultEnvironment: [String: String] {
        if self == .pathLookupWithoutPATHDescriptorExecution {
            return [
                "HOME": "/root",
                "ORLIX_DESCRIPTOR_MESSAGE": "descriptor value without path",
                "TERM": "xterm-256color"
            ]
        }

        var environment = [
            "HOME": "/root",
            "PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
            "TERM": "xterm-256color"
        ]
        if self == .descriptorExecution ||
            self == .longDescriptorExecution ||
            self == .linuxPathDescriptorExecution ||
            self == .pathLookupDescriptorExecution {
            environment["ORLIX_DESCRIPTOR_MESSAGE"] = "descriptor value with spaces"
        }
        return environment
    }

    var defaultWorkingDirectory: String {
        switch self {
        case .osRelease, .overlayMutation, .pseudoFilesystems, .ptyStdio,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify:
            return "/"
        case .descriptorExecution, .longDescriptorExecution:
            return "/tmp"
        case .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return "/tmp/.."
        }
    }

    var defaultUserID: UInt32 {
        switch self {
        case .osRelease, .overlayMutation, .linuxPathDescriptorExecution,
             .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution, .pseudoFilesystems,
             .ptyStdio, .runtimeTmpfs, .crossBootWrite, .crossBootVerify:
            return 0
        case .descriptorExecution, .longDescriptorExecution:
            return 1000
        }
    }

    var defaultGroupID: UInt32 {
        switch self {
        case .osRelease, .overlayMutation, .linuxPathDescriptorExecution,
             .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution, .pseudoFilesystems,
             .ptyStdio, .runtimeTmpfs, .crossBootWrite, .crossBootVerify:
            return 0
        case .descriptorExecution, .longDescriptorExecution:
            return 100
        }
    }

    var sendsInteractiveCommands: Bool {
        switch self {
        case .osRelease, .overlayMutation, .pseudoFilesystems, .ptyStdio,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify:
            return true
        case .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return false
        }
    }

    var commandScript: String {
        switch self {
        case .osRelease:
            return OrlixEnvironmentRootRuntimeProofRunner.osReleaseCommandScript
        case .overlayMutation:
            return [
                #"printf '%s%s\n' ORLIX_ENV_ OVERLAY_MUTATION_BEGIN"#,
                #"/bin/cat /proc/mounts"#,
                #"if ! /bin/cat /etc/os-release; then printf '%s%s\n' ORLIX_ENV_OVERLAY_ PROOF_FAILED_BASE_READ; fi"#,
                #"if printf '%s\n' ID=orlix-overlay-copyup-proof > /etc/os-release; then printf '%s%s\n' ORLIX_ENV_ OVERLAY_COPYUP_WRITE_OK; else printf '%s%s\n' ORLIX_ENV_OVERLAY_ PROOF_FAILED_COPYUP_WRITE; fi"#,
                #"if /bin/cat /etc/os-release; then printf '%s%s\n' ORLIX_ENV_ OVERLAY_COPYUP_OK; else printf '%s%s\n' ORLIX_ENV_OVERLAY_ PROOF_FAILED_COPYUP_READ; fi"#,
                #"/bin/rm /etc/os-release"#,
                #"if /bin/test ! -e /etc/os-release; then printf '%s%s\n' ORLIX_ENV_ OVERLAY_UNLINK_OK; else printf '%s%s\n' ORLIX_ENV_OVERLAY_ PROOF_FAILED_UNLINK; fi"#,
                #"/bin/sync"#,
                #"printf '%s%s\n' ORLIX_ENV_ OVERLAY_MUTATION_DONE"#,
            ].joined(separator: "\r") + "\r"
        case .pseudoFilesystems:
            return [
                #"printf '%s%s\n' ORLIX_ENV_ PSEUDOFS_BEGIN"#,
                #"if /bin/test -d /proc; then printf '%s%s\n' ORLIX_ENV_ PROC_DIR_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_PROC_DIR; fi"#,
                #"if /bin/test -r /proc/mounts; then printf '%s%s\n' ORLIX_ENV_ PROC_MOUNTS_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_PROC_MOUNTS; fi"#,
                #"if /bin/test -r /proc/self/status; then printf '%s%s\n' ORLIX_ENV_ PROC_SELF_STATUS_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_PROC_SELF_STATUS; fi"#,
                #"if /bin/test -d /proc/self/fd; then printf '%s%s\n' ORLIX_ENV_ PROC_SELF_FD_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_PROC_SELF_FD; fi"#,
                #"if /bin/test -d /dev; then printf '%s%s\n' ORLIX_ENV_ DEV_DIR_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_DEV_DIR; fi"#,
                #"if /bin/test -c /dev/null; then printf '%s%s\n' ORLIX_ENV_ DEV_NULL_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_DEV_NULL; fi"#,
                #"if /bin/test -c /dev/urandom; then printf '%s%s\n' ORLIX_ENV_ DEV_URANDOM_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_DEV_URANDOM; fi"#,
                #"if /bin/test -c /dev/tty; then printf '%s%s\n' ORLIX_ENV_ DEV_TTY_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_DEV_TTY; fi"#,
                #"if /bin/test -e /dev/ptmx; then printf '%s%s\n' ORLIX_ENV_ DEV_PTMX_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_DEV_PTMX; fi"#,
                #"if /bin/test -d /dev/pts; then printf '%s%s\n' ORLIX_ENV_ DEV_PTS_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_DEV_PTS; fi"#,
                #"if /bin/test -d /sys; then printf '%s%s\n' ORLIX_ENV_ SYS_DIR_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_SYS_DIR; fi"#,
                #"if /bin/test -r /sys/block/vda/ro; then printf '%s%s\n' ORLIX_ENV_ SYS_BLOCK_VDA_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_SYS_BLOCK_VDA; fi"#,
                #"if /bin/test -r /sys/block/vdb/ro; then printf '%s%s\n' ORLIX_ENV_ SYS_BLOCK_VDB_OK; else printf '%s%s\n' ORLIX_ENV_PSEUDOFS_ PROOF_FAILED_SYS_BLOCK_VDB; fi"#,
                #"/bin/cat /proc/self/status"#,
                #"/bin/cat /proc/mounts"#,
                #"printf '%s%s\n' ORLIX_ENV_ PSEUDOFS_DONE"#,
            ].joined(separator: "\r") + "\r"
        case .ptyStdio:
            return [
                #"printf '%s%s\n' ORLIX_ENV_ PTY_BEGIN"#,
                #"if /bin/test -t 0; then printf '%s%s\n' ORLIX_ENV_ PTY_STDIN_OK; else printf '%s%s\n' ORLIX_ENV_PTY_ PROOF_FAILED_STDIN; fi"#,
                #"if /bin/test -t 1; then printf '%s%s\n' ORLIX_ENV_ PTY_STDOUT_OK; else printf '%s%s\n' ORLIX_ENV_PTY_ PROOF_FAILED_STDOUT; fi"#,
                #"if /bin/test -t 2; then printf '%s%s\n' ORLIX_ENV_ PTY_STDERR_OK; else printf '%s%s\n' ORLIX_ENV_PTY_ PROOF_FAILED_STDERR; fi"#,
                #"if command -v tty >/dev/null 2>&1; then tty_path=$(tty); elif /bin/test -x /bin/tty; then tty_path=$(/bin/tty); elif /bin/test -x /usr/bin/tty; then tty_path=$(/usr/bin/tty); else tty_path=missing-tty-command; fi"#,
                #"printf '%s\n' "$tty_path""#,
                #"case "$tty_path" in /dev/pts/*) printf '%s%s\n' ORLIX_ENV_ PTY_PATH_OK;; missing-tty-command) printf '%s%s\n' ORLIX_ENV_PTY_ PROOF_FAILED_TTY_COMMAND;; *) printf '%s%s\n' ORLIX_ENV_PTY_ PROOF_FAILED_PATH;; esac"#,
                #"printf '%s%s\n' ORLIX_ENV_ PTY_WAITING_FOR_INPUT; IFS= read -r pty_delayed_input; if [ "$pty_delayed_input" = "orlix-pty-delayed-input" ]; then printf '%s%s\n' ORLIX_ENV_ PTY_DELAYED_INPUT_OK; else printf '%s%s\n' ORLIX_ENV_PTY_ PROOF_FAILED_DELAYED_INPUT; fi"#,
            ].joined(separator: "\r") + "\r"
        case .runtimeTmpfs:
            return [
                #"printf '%s%s\n' ORLIX_ENV_ TMPFS_BEGIN"#,
                #"mounts=$(/bin/cat /proc/mounts)"#,
                #"case "$mounts" in *" /tmp tmpfs "*) printf '%s%s\n' ORLIX_ENV_ TMP_MOUNT_OK;; *) printf '%s%s\n' ORLIX_ENV_TMPFS_ PROOF_FAILED_TMP_MOUNT;; esac"#,
                #"case "$mounts" in *" /run tmpfs "*) printf '%s%s\n' ORLIX_ENV_ RUN_MOUNT_OK;; *) printf '%s%s\n' ORLIX_ENV_TMPFS_ PROOF_FAILED_RUN_MOUNT;; esac"#,
                #"case "$mounts" in *" /dev/shm tmpfs "*) printf '%s%s\n' ORLIX_ENV_ DEV_SHM_MOUNT_OK;; *) printf '%s%s\n' ORLIX_ENV_TMPFS_ PROOF_FAILED_DEV_SHM_MOUNT;; esac"#,
                #"if printf '%s\n' tmpfs-proof > /tmp/orlix-tmpfs-proof; then printf '%s%s\n' ORLIX_ENV_ TMP_WRITE_OK; else printf '%s%s\n' ORLIX_ENV_TMPFS_ PROOF_FAILED_TMP_WRITE; fi"#,
                #"if printf '%s\n' run-proof > /run/orlix-tmpfs-proof; then printf '%s%s\n' ORLIX_ENV_ RUN_WRITE_OK; else printf '%s%s\n' ORLIX_ENV_TMPFS_ PROOF_FAILED_RUN_WRITE; fi"#,
                #"if printf '%s\n' shm-proof > /dev/shm/orlix-tmpfs-proof; then printf '%s%s\n' ORLIX_ENV_ DEV_SHM_WRITE_OK; else printf '%s%s\n' ORLIX_ENV_TMPFS_ PROOF_FAILED_DEV_SHM_WRITE; fi"#,
                #"/bin/cat /proc/mounts"#,
                #"printf '%s%s\n' ORLIX_ENV_ TMPFS_DONE"#,
            ].joined(separator: "\r") + "\r"
        case .crossBootWrite:
            return [
                #"printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_WRITE_BEGIN"#,
                #"if printf '%s\n' orlix-imported-crossboot-ok > /etc/orlix-crossboot-marker; then printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_WRITE_OK; else printf '%s%s\n' ORLIX_ENV_CROSSBOOT_ PROOF_FAILED_WRITE; fi"#,
                #"if /bin/sync; then printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_SYNC_OK; else printf '%s%s\n' ORLIX_ENV_CROSSBOOT_ PROOF_FAILED_SYNC; fi"#,
                #"marker=$(/bin/cat /etc/orlix-crossboot-marker 2>/dev/null)"#,
                #"if [ "$marker" = "orlix-imported-crossboot-ok" ]; then printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_REREAD_OK; else printf '%s%s\n' ORLIX_ENV_CROSSBOOT_ PROOF_FAILED_REREAD; fi"#,
                #"printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_WRITE_DONE"#,
            ].joined(separator: "\r") + "\r"
        case .crossBootVerify:
            return [
                #"printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_VERIFY_BEGIN"#,
                #"marker=$(/bin/cat /etc/orlix-crossboot-marker 2>/dev/null)"#,
                #"if [ "$marker" = "orlix-imported-crossboot-ok" ]; then printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_SURVIVED_OK; else printf '%s%s\n' ORLIX_ENV_CROSSBOOT_ PROOF_FAILED_VERIFY; fi"#,
                #"if /bin/rm /etc/orlix-crossboot-marker && /bin/sync; then printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_CLEANUP_OK; else printf '%s%s\n' ORLIX_ENV_CROSSBOOT_ PROOF_FAILED_CLEANUP; fi"#,
                #"printf '%s%s\n' ORLIX_ENV_ CROSSBOOT_VERIFY_DONE"#,
            ].joined(separator: "\r") + "\r"
        case .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return ""
        }
    }

    var doneMarker: String {
        switch self {
        case .osRelease:
            return "ORLIX_ENV_OS_RELEASE_DONE"
        case .overlayMutation:
            return "ORLIX_ENV_OVERLAY_MUTATION_DONE"
        case .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return "ORLIX_ENV_EXEC_DONE"
        case .pseudoFilesystems:
            return "ORLIX_ENV_PSEUDOFS_DONE"
        case .ptyStdio:
            return "ORLIX_ENV_PTY_DONE"
        case .runtimeTmpfs:
            return "ORLIX_ENV_TMPFS_DONE"
        case .crossBootWrite:
            return "ORLIX_ENV_CROSSBOOT_WRITE_DONE"
        case .crossBootVerify:
            return "ORLIX_ENV_CROSSBOOT_VERIFY_DONE"
        }
    }

    var delayedInputPrompt: String {
        switch self {
        case .ptyStdio:
            return "ORLIX_ENV_PTY_WAITING_FOR_INPUT"
        case .osRelease, .overlayMutation, .pseudoFilesystems,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify,
             .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return ""
        }
    }

    var delayedInput: String? {
        switch self {
        case .ptyStdio:
            return "orlix-pty-delayed-input\r"
        case .osRelease, .overlayMutation, .pseudoFilesystems,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify,
             .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return nil
        }
    }

    var delayedInputSuccessMarker: String {
        switch self {
        case .ptyStdio:
            return "ORLIX_ENV_PTY_DELAYED_INPUT_OK"
        case .osRelease, .overlayMutation, .pseudoFilesystems,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify,
             .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return ""
        }
    }

    var postDelayedInputScript: String? {
        switch self {
        case .ptyStdio:
            return #"printf '%s%s\n' ORLIX_ENV_ PTY_DONE"# + "\r"
        case .osRelease, .overlayMutation, .pseudoFilesystems,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify,
             .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return nil
        }
    }

    func requiredMarkers(for fixture: RuntimeFixture) -> [String] {
        switch self {
        case .osRelease:
            return [
                "ORLIX_ENV_OS_RELEASE_BEGIN",
                fixture.expectedOSReleaseID,
                "ORLIX_ENV_OS_RELEASE_DONE"
            ]
        case .overlayMutation:
            return [
                "ORLIX_ENV_OVERLAY_MUTATION_BEGIN",
                "ORLIX_ENV_OVERLAY_COPYUP_WRITE_OK",
                "ORLIX_ENV_OVERLAY_COPYUP_OK",
                "ORLIX_ENV_OVERLAY_UNLINK_OK",
                "ORLIX_ENV_OVERLAY_MUTATION_DONE"
            ]
        case .descriptorExecution, .longDescriptorExecution:
            return [
                "ORLIX_ENV_EXEC_BEGIN",
                "argv0=\(descriptorExecutionArgument0)",
                "argv1=argument with spaces",
                "env=descriptor value with spaces",
                "pwd=/tmp",
                "Uid:\t1000",
                "Gid:\t100",
                "ORLIX_ENV_EXEC_DONE"
            ]
        case .linuxPathDescriptorExecution:
            return [
                "ORLIX_ENV_EXEC_BEGIN",
                "argv0=linux-path-argv0",
                "argv1=",
                "argv2=argument after empty",
                "pwd=/",
                "ORLIX_ENV_EXEC_DONE"
            ]
        case .pathLookupDescriptorExecution:
            return [
                "ORLIX_ENV_EXEC_BEGIN",
                "argv0=path-lookup-argv0",
                "argv1=argument after path lookup",
                "pwd=/",
                "ORLIX_ENV_EXEC_DONE"
            ]
        case .pathLookupWithoutPATHDescriptorExecution:
            return [
                "ORLIX_ENV_EXEC_BEGIN",
                "argv0=path-fallback-argv0",
                "argv1=argument after fallback path lookup",
                "env=descriptor value without path",
                "pwd=/",
                "ORLIX_ENV_EXEC_DONE"
            ]
        case .pseudoFilesystems:
            return [
                "ORLIX_ENV_PSEUDOFS_BEGIN",
                "ORLIX_ENV_PROC_DIR_OK",
                "ORLIX_ENV_PROC_MOUNTS_OK",
                "ORLIX_ENV_PROC_SELF_STATUS_OK",
                "ORLIX_ENV_PROC_SELF_FD_OK",
                "ORLIX_ENV_DEV_DIR_OK",
                "ORLIX_ENV_DEV_NULL_OK",
                "ORLIX_ENV_DEV_URANDOM_OK",
                "ORLIX_ENV_DEV_TTY_OK",
                "ORLIX_ENV_DEV_PTMX_OK",
                "ORLIX_ENV_DEV_PTS_OK",
                "ORLIX_ENV_SYS_DIR_OK",
                "ORLIX_ENV_SYS_BLOCK_VDA_OK",
                "ORLIX_ENV_SYS_BLOCK_VDB_OK",
                "ORLIX_ENV_PSEUDOFS_DONE"
            ]
        case .ptyStdio:
            return [
                "ORLIX_ENV_PTY_BEGIN",
                "ORLIX_ENV_PTY_STDIN_OK",
                "ORLIX_ENV_PTY_STDOUT_OK",
                "ORLIX_ENV_PTY_STDERR_OK",
                "ORLIX_ENV_PTY_PATH_OK",
                "ORLIX_ENV_PTY_WAITING_FOR_INPUT",
                "ORLIX_ENV_PTY_DELAYED_INPUT_OK",
                "ORLIX_ENV_PTY_DONE"
            ]
        case .runtimeTmpfs:
            return [
                "ORLIX_ENV_TMPFS_BEGIN",
                "ORLIX_ENV_TMP_MOUNT_OK",
                "ORLIX_ENV_RUN_MOUNT_OK",
                "ORLIX_ENV_DEV_SHM_MOUNT_OK",
                "ORLIX_ENV_TMP_WRITE_OK",
                "ORLIX_ENV_RUN_WRITE_OK",
                "ORLIX_ENV_DEV_SHM_WRITE_OK",
                "ORLIX_ENV_TMPFS_DONE"
            ]
        case .crossBootWrite:
            return [
                "ORLIX_ENV_CROSSBOOT_WRITE_BEGIN",
                "ORLIX_ENV_CROSSBOOT_WRITE_OK",
                "ORLIX_ENV_CROSSBOOT_SYNC_OK",
                "ORLIX_ENV_CROSSBOOT_REREAD_OK",
                "ORLIX_ENV_CROSSBOOT_WRITE_DONE"
            ]
        case .crossBootVerify:
            return [
                "ORLIX_ENV_CROSSBOOT_VERIFY_BEGIN",
                "ORLIX_ENV_CROSSBOOT_SURVIVED_OK",
                "ORLIX_ENV_CROSSBOOT_CLEANUP_OK",
                "ORLIX_ENV_CROSSBOOT_VERIFY_DONE"
            ]
        }
    }

    private var descriptorExecutionScript: String {
        switch self {
        case .descriptorExecution, .longDescriptorExecution,
             .linuxPathDescriptorExecution, .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution:
            return Self.descriptorExecutionScript
        case .osRelease, .overlayMutation, .pseudoFilesystems, .ptyStdio,
             .runtimeTmpfs, .crossBootWrite, .crossBootVerify:
            return ""
        }
    }

    private var descriptorExecutionArgument0: String {
        switch self {
        case .longDescriptorExecution:
            return "orlix-descriptor-" + String(repeating: "x", count: 256)
        case .descriptorExecution:
            return "orlix-descriptor-" + String(repeating: "x", count: 16)
        case .osRelease, .overlayMutation, .linuxPathDescriptorExecution,
             .pathLookupDescriptorExecution,
             .pathLookupWithoutPATHDescriptorExecution, .pseudoFilesystems,
             .ptyStdio, .runtimeTmpfs, .crossBootWrite, .crossBootVerify:
            return ""
        }
    }

    private static let descriptorExecutionScript = (
        [": descriptor-start"] + descriptorExecutionLines
    ).joined(separator: "\n")

    private static let descriptorExecutionLines = [
        #"printf '%s%s\n' ORLIX_ENV_ EXEC_BEGIN"#,
        #"printf 'argv0=%s\n' "$0""#,
        #"printf 'argv1=%s\n' "$1""#,
        #"printf 'argv2=%s\n' "$2""#,
        #"printf 'env=%s\n' "$ORLIX_DESCRIPTOR_MESSAGE""#,
        #"printf pwd=; pwd"#,
        #"/bin/cat /etc/os-release"#,
        #"/bin/cat /proc/self/status"#,
        #"printf '%s%s\n' ORLIX_ENV_ EXEC_DONE"#,
    ]
}

private enum RuntimeFixture: Sendable {
    case tarDerived
    case ociDerived

    var directoryName: String {
        switch self {
        case .tarDerived:
            return "tar-imported"
        case .ociDerived:
            return "oci-imported"
        }
    }

    var description: String {
        switch self {
        case .tarDerived:
            return "tar-derived"
        case .ociDerived:
            return "OCI-derived"
        }
    }

    var environmentID: String {
        switch self {
        case .tarDerived:
            return "tar-imported-runtime-proof"
        case .ociDerived:
            return "oci-imported-runtime-proof"
        }
    }

    var copiedEnvironmentID: String {
        "\(environmentID)-copy"
    }

    var source: OrlixEnvironmentSource {
        switch self {
        case .tarDerived:
            return .rootfsTar
        case .ociDerived:
            return .ociLayout
        }
    }

    var rootImageIdentifier: String {
        switch self {
        case .tarDerived:
            return "orlix.test.environment.tar-runtime-proof"
        case .ociDerived:
            return "orlix.test.environment.oci-runtime-proof"
        }
    }

    var copiedRootImageIdentifier: String {
        "\(rootImageIdentifier).copy"
    }

    var expectedOSReleaseID: String {
        switch self {
        case .tarDerived:
            return "ID=orlix-tar-runtime-proof"
        case .ociDerived:
            return "ID=orlix-oci-runtime-proof"
        }
    }
}

private struct EnvironmentRootFixture {
    let root: URL

    var linuxStateRoot: URL {
        root.appendingPathComponent("state", isDirectory: true)
    }

    var cacheRoot: URL {
        root.appendingPathComponent("cache", isDirectory: true)
    }

    var scratchRoot: URL {
        root.appendingPathComponent("scratch", isDirectory: true)
    }
}

private enum OrlixEnvironmentRootRuntimeProofError:
    Error,
    CustomStringConvertible
{
    case bootFailed(OrlixBootStatus)
    case noTerminalOutput(TimeInterval, URL)
    case timeout(TimeInterval, String, URL, String)
    case fatalMarker(String)
    case missingMarker(String, URL)
    case staleFixture(String)

    var description: String {
        switch self {
        case let .bootFailed(status):
            return "Orlix imported environment boot failed: \(status.message)"
        case let .noTerminalOutput(timeout, logURL):
            return "no Linux terminal output after \(Int(timeout)) seconds; terminal log: \(logURL.path)"
        case let .timeout(timeout, output, logURL, tail):
            return "timed out after \(Int(timeout)) seconds waiting for imported environment proof output; terminal log: \(logURL.path); tail: \(tail); output: \(output)"
        case let .fatalMarker(marker):
            return "fatal imported environment runtime marker found: \(marker)"
        case let .missingMarker(marker, logURL):
            return "missing imported environment marker \(marker); terminal log: \(logURL.path)"
        case let .staleFixture(message):
            return "stale imported environment runtime fixture: \(message)"
        }
    }
}

private final class EnvironmentRootTerminalLog: @unchecked Sendable {
    let url: URL

    private let lock = NSLock()

    init() {
        let fileName = "orlix-environment-root-\(UUID().uuidString).log"
        url = FileManager.default.temporaryDirectory.appendingPathComponent(fileName)
        FileManager.default.createFile(atPath: url.path, contents: nil)
    }

    func writeLine(_ line: String) {
        append(Data("[orlix-environment-root] \(line)\n".utf8))
    }

    func append(_ data: Data) {
        lock.lock()
        defer { lock.unlock() }

        guard let handle = FileHandle(forWritingAtPath: url.path) else {
            return
        }
        handle.seekToEndOfFile()
        handle.write(data)
        handle.closeFile()
    }

    func tail(limit: Int = 4096) -> String {
        lock.lock()
        defer { lock.unlock() }

        guard let data = try? Data(contentsOf: url) else {
            return ""
        }
        return String(decoding: data.suffix(limit), as: UTF8.self)
    }
}

private final class EnvironmentRootBootStatusRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var storage: OrlixBootStatus?

    var value: OrlixBootStatus? {
        lock.lock()
        defer { lock.unlock() }
        return storage
    }

    func set(_ status: OrlixBootStatus) {
        lock.lock()
        storage = status
        lock.unlock()
    }
}

private final class EnvironmentRootOutputRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private let terminalLog: EnvironmentRootTerminalLog
    private var storage = Data()
    private var sentProofCommands = false
    private var sentDelayedInput = false
    private var sentPostDelayedInputScript = false

    init(terminalLog: EnvironmentRootTerminalLog) {
        self.terminalLog = terminalLog
    }

    var text: String {
        lock.lock()
        defer { lock.unlock() }
        return String(decoding: storage, as: UTF8.self)
    }

    var hasSentProofCommands: Bool {
        lock.lock()
        defer { lock.unlock() }
        return sentProofCommands
    }

    var hasSentDelayedInput: Bool {
        lock.lock()
        defer { lock.unlock() }
        return sentDelayedInput
    }

    var hasSentPostDelayedInputScript: Bool {
        lock.lock()
        defer { lock.unlock() }
        return sentPostDelayedInputScript
    }

    var byteCount: Int {
        lock.lock()
        defer { lock.unlock() }
        return storage.count
    }

    func append(_ data: Data) {
        lock.lock()
        storage.append(data)
        lock.unlock()
        terminalLog.append(data)
    }

    func markProofCommandsSent() {
        lock.lock()
        sentProofCommands = true
        lock.unlock()
    }

    func markDelayedInputSent() {
        lock.lock()
        sentDelayedInput = true
        lock.unlock()
    }

    func markPostDelayedInputScriptSent() {
        lock.lock()
        sentPostDelayedInputScript = true
        lock.unlock()
    }
}
