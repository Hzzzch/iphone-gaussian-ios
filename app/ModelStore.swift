import Foundation
import CryptoKit

enum ModelStore {
    struct Entry: Decodable { let bytes: Int64; let sha256: String }
    struct Manifest: Decodable { let format: String; let files: [String: Entry] }
    static var documents: URL { FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0] }
    static var installed: URL { documents.appendingPathComponent("Models", isDirectory: true) }
    static func fail(_ text: String) -> NSError { NSError(domain: "MobileGS", code: 1, userInfo: [NSLocalizedDescriptionKey: text]) }
    static func expected() throws -> Manifest {
        guard let url = Bundle.main.url(forResource: "model-manifest", withExtension: "json") else { throw fail("缺少模型清单") }
        return try JSONDecoder().decode(Manifest.self, from: Data(contentsOf: url))
    }
    static func verify(_ folder: URL) throws {
        let manifest = try expected()
        for (name, entry) in manifest.files {
            let url = folder.appendingPathComponent(name)
            let values = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            guard values.isRegularFile == true, values.isSymbolicLink != true else { throw fail("模型不是普通文件：\(name)") }
            let file = try FileHandle(forReadingFrom: url)
            defer { try? file.close() }
            var hash = SHA256(); var count: Int64 = 0
            while let data = try file.read(upToCount: 4 * 1024 * 1024), !data.isEmpty {
                count += Int64(data.count); hash.update(data: data)
                if count > entry.bytes { throw fail("模型大小不符：\(name)") }
            }
            let digest = hash.finalize().map { String(format: "%02x", $0) }.joined()
            guard count == entry.bytes, digest == entry.sha256 else { throw fail("模型校验失败：\(name)，请使用 iOS ONNX 模型包") }
        }
    }
    static func install(from source: URL) throws {
        let access = source.startAccessingSecurityScopedResource()
        defer { if access { source.stopAccessingSecurityScopedResource() } }
        let fm = FileManager.default
        let staging = documents.appendingPathComponent("Models-staging-" + UUID().uuidString)
        try fm.createDirectory(at: staging, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: staging) }
        for name in try expected().files.keys {
            try fm.copyItem(at: source.appendingPathComponent(name), to: staging.appendingPathComponent(name))
        }
        try verify(staging)
        if fm.fileExists(atPath: installed.path) {
            _ = try fm.replaceItemAt(installed, withItemAt: staging)
        } else { try fm.moveItem(at: staging, to: installed) }
        var directory = installed
        var values = URLResourceValues(); values.isExcludedFromBackup = true
        try directory.setResourceValues(values)
    }
}
