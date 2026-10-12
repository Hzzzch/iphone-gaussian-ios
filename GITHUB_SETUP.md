# GitHub 与 Mac 接续开发

源码已上传到 [Hzzzch/iphone-gaussian-ios](https://github.com/Hzzzch/iphone-gaussian-ios)，分支为 `main`。完整模型位于 [models-onnx-scaled-v1 Release](https://github.com/Hzzzch/iphone-gaussian-ios/releases/tag/models-onnx-scaled-v1)。

## Mac 登录与获取源码

Mac 的 SSH 密钥独立于 Windows，需要在 Mac 上另行配置或使用 GitHub CLI 的 HTTPS 登录。私钥不要放进项目。

```bash
brew install gh xcodegen cocoapods python
ssh -T git@github.com
git clone git@github.com:Hzzzch/iphone-gaussian-ios.git
cd iphone-gaussian-ios
```

上面的克隆方式要求 Mac 已配置自己的 GitHub SSH 密钥。也可安装 `gh`，使用 `gh auth login --hostname github.com --git-protocol https --web` 完成网页登录，再用 `gh repo clone Hzzzch/iphone-gaussian-ios` 克隆。私有 Release 下载需要 GitHub CLI 的 API 授权，SSH 不能替代它。

## 模型

三个完整 ONNX 合计约 2.54 GB，作为 Release 附件保存，不纳入 Git 历史，不依赖 Git LFS。Mac 不需要连接内网服务器即可下载。先登录有仓库访问权限的 GitHub 账号：

```bash
gh auth login --hostname github.com --web
python3 tools/fetch_models.py --repo Hzzzch/iphone-gaussian-ios
```

脚本按源码中固定清单核对大小和 SHA-256。失败时保留已下载文件，重试请用新的 `--out` 目录。

下载结果在 `models/onnx-scaled-v1`。也可在 Release 页面手动下载三个 ONNX，放在同一文件夹中。`manifest.json` 和 `SHA256SUMS` 提供校验信息。原内网下载方式仍可使用：

```bash
python3 tools/fetch_models.py --host hezic@10.15.88.93
```

使用 `depth.onnx`、`features.onnx`、`head.onnx` 三个完整模型，不要用 Android 的 QNN 缓存替代。

## 构建

先安装完整 Xcode，打开完成组件安装，选择支持目标手机的 iOS 平台。

```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -runFirstLaunch
bash tools/build_ios.sh
open GaussianIOS.xcworkspace
```

在 Xcode 设置签名 Team 后安装到 iPhone，将模型文件夹传到手机并在 Demo 内导入。首版尚未完成 Apple 平台编译和真机验收，详见 README 与 reports/status.json。
