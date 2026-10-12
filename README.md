# iPhone / iOS 照片重建移植

独立工作目录：`Server/iphone-ios`。基于 Android V6 的 C++ 重建流水线与 V5 缩放特征网络建立 iOS 首版；Android 原目录和 GitLab 分支未修改。此目录目前未上传 GitLab。

**状态：已完成首版移植代码与 Linux 共享核心回归；尚未完成 Xcode 编译、iPhone 安装、Core ML / ANE 真机验收，没有 IPA。** 当前环境为 Windows + Linux 服务器，还需提供可用的 Mac / Xcode、目标 iPhone 型号和系统版本。

## 实现内容

- SwiftUI Demo：模型文件夹导入、SHA-256 校验、示例照片／系统选图、后台重建、耗时显示、PLY 分享。
- Objective-C++ 桥接：照片方向处理、最长边 1280、RGB8 输入、三模型常驻会话；沿用 518 中心裁剪与抗锯齿预处理、稳定采样和 10 万高斯。
- ONNX Runtime 后端：`cpu`、`coreml`（ALL）、`coreml-ane`（CPUAndNeuralEngine）、`coreml-gpu`（CPUAndGPU）。Core ML 使用 MLProgram，允许不支持的算子落到 CPU；注册失败明确报错，不静默重试全 CPU。
- CPU 工作线程固定为 4 并关闭空转；界面可释放常驻会话，释放时写出 ORT profiling。性能日志与场景在应用 Documents/Runs 下，可通过文件共享取出。
- 复用高斯 PLY 导出与 Three.js / GaussianSplats3D，WKWebView 用受限本地 scheme 提供资源。浏览器渲染属于 WebGL 路径，不是专用 Metal 高斯渲染器，iOS 上尚未验证。

选择 `CPUAndNeuralEngine` 只是允许 Core ML 调度 ANE，并不证明算子全部在 ANE 执行。ORT profiling 可分析 Core ML 与 CPU 的划分；ANE/GPU 硬件占用仍需 Apple 工具实测。首版默认 CPU 对照，避免把未验证加速视为已完成。

## 模型在哪里

服务器 `10.15.88.93` 已准备并校验：

```text
/storage0/home/hezic/iphone-ios/models/onnx-scaled-v1/
  depth.onnx         1,305,439,605 bytes
  features.onnx      1,223,385,310 bytes
  head.onnx             14,243,756 bytes
  manifest.json
  model-audit.json
```

三个模型合计 2,543,068,671 字节（约 2.54 GB）。深度和高斯头使用原始完整 ONNX，特征模型使用之前验证过的 residual 0.25 等价缩放版本。服务器用硬链接保留源模型，禁止原地修改；后续转换应另写输出路径。

这些是 iOS 首版的 ONNX 输入模型，不是已编译的 `.mlmodelc`。由 Core ML EP 在 Apple 设备上转换支持的子图。Android 的 `models-sm8850.zip`、`*_qnn.bin` 和 EPContext ONNX **不能用于这个 iOS Demo**。

从当前电脑或已配置 SSH 的 Mac 获取完整模型文件夹：

```bash
python3 tools/fetch_models.py --host gaussian-server
# 或 --host hezic@10.15.88.93
```

下载后在 `models/onnx-scaled-v1`，脚本会核对打包在源码中的清单。该目录约 2.54 GB，默认不进 Git；本地尚未自动下载这份权重。`reports/model-audit.json` 包含实际输入输出、算子分布和抗锯齿 Resize 节点，静态检查不代表 Core ML 算子通过。

## 在 Mac 构建和安装

1. 准备能支持目标手机系统的 Xcode，安装 XcodeGen、CocoaPods。当前部署下限 iOS 16.4（查看器使用 import map）。
2. 将整个 `iphone-ios` 目录复制到 Mac。在该目录执行：

```bash
bash tools/build_ios.sh
```

脚本生成 `GaussianIOS.xcodeproj`，通过 CocoaPods 安装锁定的 `onnxruntime-c 1.20.0`，然后执行真机架构的无签名编译。共享核心服务器测试使用现有 ORT 1.20.1；两者的设备结果需要重新对比。当前未执行 `pod install` / Xcode 编译，不能宣称依赖链接已经通过。

3. 打开 `GaussianIOS.xcworkspace`，在 Signing & Capabilities 选择开发者 Team，并按账户需要更改 bundle ID。连接 iPhone，按手机和 Xcode 提示完成信任／开发者模式，然后 Run。
4. 将下载好的 `onnx-scaled-v1` 文件夹放到 iPhone 的“文件”可访问位置，先确保云盘中的大文件已下载。打开 Demo，点击“导入 iOS 模型文件夹”，选择包含三个 ONNX 的文件夹，等待复制及校验。
5. 使用示例照片先跑 CPU 对照，再跑 Core ML 自动／CPU+ANE／CPU+GPU；默认示例 FOV 为 49.7776°。自选照片的 68° 是假定值，描述的是中心裁剪后的正方形视场角。
6. 点击“查看 3D 场景”或“导出 PLY”。每种后端分别记录首次初始化和预热后重建时间，切换后端会释放旧会话。点击“释放模型并保存性能日志”完成日志写出。

模型导入会同时占用源文件与应用内副本约 5.1 GB，后续还有编译缓存和场景输出，建议至少预留 8 GB。当前模型和三会话可能消耗较大内存，目标 iPhone 的峰值内存、系统终止风险与温升都待测。重建期间请保持前台，尚未实现后台长任务恢复或任务取消。

## 已完成的验证

`reports/host-validation.json`：Linux 原生编译与几何测试通过；用相同模型、同一张 640×480 示例图分别运行原 V6 和 iOS 移植核心，RGB、深度、采样坐标、面积、位置、协方差、颜色、透明度八组输出逐值相同。PLY 协方差重建相对 L2 为 `1.4382e-7`。

Core ML C++ API 分支已用 ORT 头文件编译通过；Linux 无 Core ML 时会明确拒绝该后端。以上验证不能替代 Apple 平台编译、UIKit 图像方向、WKWebView 查看器、模型导入或设备推理验收。

## 后续验收顺序

1. Mac 无签名编译、签名安装，核对照片方向与颜色、模型导入和场景显示。
2. iPhone 同图 CPU 对照，再比较 Core ML 三种计算配置；检查实际子图划分、首次编译时间、预热耗时、峰值内存。
3. 重跑盆栽及 courtyard / room / bedroom / cave / pipes，保留失败。画质初筛延用三视角 PSNR≥30、SSIM≥0.97、源图 PSNR 下降≤0.5 dB；画质允许少量损失，速度是否达标需同机 CPU 对比。
4. 根据真实瓶颈决定静态形状整理、FP16／压缩、逐段加载减少内存或专用 Metal 渲染；首版不预先宣称这些优化已完成。

## 资料与许可

- [ONNX Runtime Core ML EP](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html)
- [ONNX Runtime iOS 构建](https://onnxruntime.ai/docs/build/ios.html)
- [XcodeGen 项目规范](https://github.com/yonaskolb/XcodeGen/blob/master/Docs/ProjectSpec.md)
- 模型来源许可见 `LICENSES/`，浏览器组件许可见 `app/Resources/viewer/`。
