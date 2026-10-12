import SwiftUI
import WebKit

// Only bundled viewer resources and two result files are exposed to JavaScript.
final class LocalSceneHandler: NSObject, WKURLSchemeHandler {
    let directory: URL
    init(_ directory: URL) { self.directory = directory }
    func webView(_ webView: WKWebView, start urlSchemeTask: WKURLSchemeTask) {
        guard let url = urlSchemeTask.request.url, url.host == "viewer" else {
            urlSchemeTask.didFailWithError(ModelStore.fail("无效场景地址")); return
        }
        let files = ["/viewer.html": "text/html", "/three.module.js": "application/javascript",
                     "/gaussian-splats-3d.module.js": "application/javascript",
                     "/result/scene_browser.ply": "application/octet-stream", "/result/viewer_config.json": "application/json"]
        guard let mime = files[url.path], let resources = Bundle.main.url(forResource: "viewer", withExtension: nil) else {
            urlSchemeTask.didFailWithError(ModelStore.fail("场景资源不存在")); return
        }
        let file = url.path.hasPrefix("/result/") ? directory.appendingPathComponent(url.lastPathComponent) : resources.appendingPathComponent(url.lastPathComponent)
        do {
            let data = try Data(contentsOf: file)
            urlSchemeTask.didReceive(URLResponse(url: url, mimeType: mime, expectedContentLength: data.count, textEncodingName: mime.hasPrefix("text/") || mime.contains("javascript") || mime.contains("json") ? "utf-8" : nil))
            urlSchemeTask.didReceive(data); urlSchemeTask.didFinish()
        } catch { urlSchemeTask.didFailWithError(error) }
    }
    func webView(_ webView: WKWebView, stop urlSchemeTask: WKURLSchemeTask) {}
}
struct SceneView: UIViewRepresentable {
    let directory: URL
    func makeUIView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.setURLSchemeHandler(LocalSceneHandler(directory), forURLScheme: "gs-local")
        let view = WKWebView(frame: .zero, configuration: config)
        view.load(URLRequest(url: URL(string: "gs-local://viewer/viewer.html")!))
        return view
    }
    func updateUIView(_ uiView: WKWebView, context: Context) {}
    static func dismantleUIView(_ uiView: WKWebView, coordinator: ()) { uiView.stopLoading(); uiView.loadHTMLString("", baseURL: nil) }
}
