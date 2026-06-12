import Foundation
import XCTest

final class ArchitectureInvariantTests: XCTestCase {
    private lazy var root: URL = {
        var url = URL(fileURLWithPath: #filePath)
        while url.path != "/" {
            let candidate = url.deletingLastPathComponent()
            if FileManager.default.fileExists(atPath: candidate.appendingPathComponent("project.yml").path) {
                return candidate
            }
            url = candidate
        }
        XCTFail("could not locate repository root from \(#filePath)")
        return URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
    }()

    func testActiveProductSourcesDoNotUseHistoricalBranding() throws {
        let forbidden = [
            "IXLand",
            "ixland",
            "IXLAND",
            "OrlixKit"
        ]
        let files = try sourceFiles(under: [
            "project.yml",
            "OrlixKernel/Sources",
            "OrlixOS/Sources",
            "OrlixHostAdapter/Sources",
            "OrlixTerminal/Sources",
            "OrlixTestRunner/Sources",
            "tools"
        ])

        let hits = try matchingLines(in: files) { line in
            forbidden.contains { line.contains($0) }
        }

        XCTAssertTrue(hits.isEmpty, hits.joined(separator: "\n"))
    }

    func testOrlixKernelLinuxOwnerCodeDoesNotIncludeHostHeaders() throws {
        let runtimeOverlayFiles = try sourceFiles(under: [
            "OrlixKernel/Sources/ports/orlix/overlay/arch/orlix",
            "OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix"
        ])
        let forbiddenIncludeFragments = [
            "<CoreFoundation/",
            "<Foundation/",
            "<Darwin/",
            "<mach/",
            "<pthread.h>",
            "<unistd.h>",
            "<sys/",
            "<fcntl.h>",
            "<errno.h>",
            "<stdio.h>",
            "<stdlib.h>",
            "<string.h>",
            "\"OrlixHostAdapter",
            "<OrlixHostAdapter"
        ]

        let hits = try matchingLines(in: runtimeOverlayFiles) { line in
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            guard trimmed.hasPrefix("#include") else {
                return false
            }
            return forbiddenIncludeFragments.contains { trimmed.contains($0) }
        }

        XCTAssertTrue(hits.isEmpty, hits.joined(separator: "\n"))
    }

    func testIOSRuntimeTargetsDoNotDependOnAppleContainerOrVirtualizationRuntime() throws {
        let files = try sourceFiles(under: [
            "project.yml",
            "OrlixKernel/Sources",
            "OrlixOS/Sources",
            "OrlixHostAdapter/Sources",
            "OrlixTerminal/Sources",
            "OrlixTestRunner/Sources"
        ])
        let forbidden = [
            "AppleContainer",
            "apple/container",
            "Containerization",
            "Virtualization.framework",
            "Virtualization"
        ]

        let hits = try matchingLines(in: files) { line in
            forbidden.contains { line.contains($0) }
        }

        XCTAssertTrue(hits.isEmpty, hits.joined(separator: "\n"))
    }

    func testOrlixKernelOverlayDoesNotDefineLocalLinuxUAPIClones() throws {
        let files = try sourceFiles(under: [
            "OrlixKernel/Sources/ports/orlix/overlay/arch/orlix",
            "OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix"
        ]).filter { !$0.path.contains("/tools/testing/selftests/") }

        let clonedUAPIDefine = try NSRegularExpression(
            pattern: #"^\s*#\s*define\s+(__NR_|SYS_|FUTEX_|CLONE_|O_[A-Z0-9_]+|AT_[A-Z0-9_]+|AF_[A-Z0-9_]+|VIRTIO_|VIRTIO_MMIO_|VIRTIO_BLK_)"#
        )
        let hits = try matchingLines(in: files) { line in
            let range = NSRange(line.startIndex..<line.endIndex, in: line)
            return clonedUAPIDefine.firstMatch(in: line, range: range) != nil
        }

        XCTAssertTrue(hits.isEmpty, hits.joined(separator: "\n"))
    }

    private func sourceFiles(under relativePaths: [String]) throws -> [URL] {
        let manager = FileManager.default
        var urls: [URL] = []
        for relativePath in relativePaths {
            let url = root.appendingPathComponent(relativePath)
            var isDirectory: ObjCBool = false
            guard manager.fileExists(atPath: url.path, isDirectory: &isDirectory) else {
                continue
            }
            if isDirectory.boolValue {
                guard let enumerator = manager.enumerator(
                    at: url,
                    includingPropertiesForKeys: [.isRegularFileKey],
                    options: [.skipsHiddenFiles]
                ) else {
                    continue
                }
                for case let fileURL as URL in enumerator {
                    let relative = fileURL.path.replacingOccurrences(of: root.path + "/", with: "")
                    if shouldSkip(relativePath: relative) {
                        enumerator.skipDescendants()
                        continue
                    }
                    let values = try fileURL.resourceValues(forKeys: [.isRegularFileKey])
                    if values.isRegularFile == true, isScannedSource(fileURL) {
                        urls.append(fileURL)
                    }
                }
            } else if isScannedSource(url) {
                urls.append(url)
            }
        }
        return urls.sorted { $0.path < $1.path }
    }

    private func shouldSkip(relativePath: String) -> Bool {
        relativePath.hasPrefix("Build/")
            || relativePath.contains("/Build/")
            || relativePath.hasPrefix(".git/")
            || relativePath.contains("/.git/")
            || relativePath.hasPrefix(".deriveddata/")
            || relativePath.contains("/.deriveddata/")
    }

    private func isScannedSource(_ url: URL) -> Bool {
        let allowedExtensions = [
            "c",
            "cc",
            "cpp",
            "h",
            "m",
            "mm",
            "swift",
            "yml",
            "yaml",
            "mk",
            "dts",
            "S",
            "sh",
            "pl"
        ]
        return allowedExtensions.contains(url.pathExtension) || url.lastPathComponent == "project.yml"
    }

    private func matchingLines(
        in files: [URL],
        where predicate: (String) -> Bool
    ) throws -> [String] {
        var hits: [String] = []
        for file in files {
            let contents = try String(contentsOf: file)
            let relativePath = file.path.replacingOccurrences(of: root.path + "/", with: "")
            for (index, line) in contents.components(separatedBy: .newlines).enumerated() {
                if predicate(line) {
                    hits.append("\(relativePath):\(index + 1): \(line)")
                }
            }
        }
        return hits
    }
}
