import XCTest
@testable import OrlixKit

final class OrlixTerminalSessionTests: XCTestCase {
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
