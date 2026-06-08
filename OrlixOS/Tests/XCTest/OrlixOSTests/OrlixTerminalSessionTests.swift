import XCTest
@_spi(OrlixPrivateTesting) @testable import OrlixOS

final class OrlixTerminalSessionTests: XCTestCase {
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
