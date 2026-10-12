import SwiftUI
import PhotosUI
import UniformTypeIdentifiers

@main struct GaussianApp: App {
    var body: some Scene { WindowGroup { ContentView() } }
}

final class DemoState: ObservableObject {
    @Published var message = "请先导入 iOS ONNX 模型文件夹"
    @Published var busy = false
    @Published var image: UIImage? = UIImage(named: "example")
    @Published var result: URL?
    private let queue = DispatchQueue(label: "mobilegs.inference", qos: .userInitiated)
    private let engine = GSEngine()
    private var verified = false
    func install(_ folder: URL) {
        busy = true; message = "正在复制并校验三个模型…"
        queue.async {
            do {
                self.engine.close(); self.verified = false
                try ModelStore.install(from: folder); self.verified = true
                DispatchQueue.main.async { self.message = "模型已就绪，可离线重建"; self.busy = false }
            } catch { self.failed(error) }
        }
    }
    func run(backend: String, fov: Double) {
        guard let image = image else { message = "请先选择照片"; return }
        busy = true; result = nil; message = "正在准备模型并重建，首次 Core ML 编译可能较慢…"
        queue.async {
            do {
                if !self.verified { try ModelStore.verify(ModelStore.installed); self.verified = true }
                let runs = ModelStore.documents.appendingPathComponent("Runs", isDirectory: true)
                try FileManager.default.createDirectory(at: runs, withIntermediateDirectories: true)
                let output = runs.appendingPathComponent(UUID().uuidString, isDirectory: true)
                let report = try self.engine.reconstruct(image, models: ModelStore.installed, output: output, backend: backend, fov: fov)
                let setup = (report["setup_seconds"] as? NSNumber)?.doubleValue ?? 0
                let run = (report["reconstruct_seconds"] as? NSNumber)?.doubleValue ?? 0
                DispatchQueue.main.async {
                    self.result = output; self.busy = false
                    self.message = String(format: "已生成 10 万高斯\n模型准备 %.2f 秒；重建 %.2f 秒", setup, run)
                }
            } catch { self.failed(error) }
        }
    }
    func close() {
        busy = true
        queue.async { self.engine.close(); DispatchQueue.main.async { self.busy = false; self.message = "会话已释放，性能日志已写入 Runs" } }
    }
    private func failed(_ error: Error) {
        DispatchQueue.main.async { self.message = error.localizedDescription; self.busy = false }
    }
}

struct ContentView: View {
    @StateObject private var state = DemoState()
    @State private var backend = "cpu"
    @State private var fov = "49.7776"
    @State private var importing = false
    @State private var photo: PhotosPickerItem?
    @State private var showScene = false
    var body: some View {
        NavigationStack {
            Form {
                Section("模型与照片") {
                    Button("导入 iOS 模型文件夹") { importing = true }
                    Button("使用示例照片") { state.image = UIImage(named: "example"); fov = "49.7776" }
                    PhotosPicker("选择照片", selection: $photo, matching: .images)
                    if let image = state.image { Image(uiImage: image).resizable().scaledToFit().frame(maxHeight: 220) }
                }
                Section("重建") {
                    Picker("计算方式", selection: $backend) {
                        Text("CPU 对照").tag("cpu")
                        Text("Core ML 自动").tag("coreml")
                        Text("Core ML · CPU / 神经引擎").tag("coreml-ane")
                        Text("Core ML · CPU / GPU").tag("coreml-gpu")
                    }
                    TextField("裁剪后视场角（10–120°）", text: $fov).keyboardType(.decimalPad)
                    Button("开始重建") {
                        guard let angle = Double(fov), angle.isFinite, (10...120).contains(angle) else { state.message = "请输入 10–120° 的视场角"; return }
                        state.run(backend: backend, fov: angle)
                    }
                    Button("释放模型并保存性能日志") { state.close() }
                    Text("Core ML 允许 CPU 执行不支持的算子。选择神经引擎模式不代表全部算子都在 ANE 上运行。")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                Section("结果") {
                    Text(state.message).textSelection(.enabled)
                    if state.busy { ProgressView() }
                    if let result = state.result {
                        Button("查看 3D 场景") { showScene = true }
                        ShareLink("导出 PLY", item: result.appendingPathComponent("scene.ply"))
                    }
                }
            }
            .disabled(state.busy)
            .navigationTitle("iPhone 照片重建")
            .fileImporter(isPresented: $importing, allowedContentTypes: [.folder]) { result in
                switch result { case .success(let folder): state.install(folder)
                case .failure(let error): state.message = error.localizedDescription }
            }
            .onChange(of: photo) { item in
                Task {
                    do {
                        guard let data = try await item?.loadTransferable(type: Data.self), let image = UIImage(data: data) else { return }
                        state.image = image; fov = "68"
                    } catch { state.message = error.localizedDescription }
                }
            }
            .sheet(isPresented: $showScene) {
                if let result = state.result { NavigationStack { SceneView(directory: result).toolbar { Button("关闭") { showScene = false } } } }
            }
        }
    }
}
