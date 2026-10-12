# GitHub 与 Mac 接续开发

计划使用私有仓库 `Hzzzch/iphone-gaussian-ios`；最终地址等待仓库创建后确认。本说明不表示上传已完成。

## Mac 登录与获取源码

Mac 的 SSH 密钥独立于 Windows，需要在 Mac 上另行配置或使用 GitHub CLI 的 HTTPS 登录。私钥不要放进项目。

```bash
brew install gh xcodegen cocoapods python
gh auth login --hostname github.com --git-protocol https --web
gh repo clone Hzzzch/iphone-gaussian-ios
cd iphone-gaussian-ios
```

如果 Mac 已配置 GitHub SSH，可用 `ssh -T git@github.com` 验证，再用 SSH 地址克隆。私有 Release 下载仍需 GitHub CLI 的 API 授权。

## 模型

三个完整 ONNX 计划作为 `models-onnx-scaled-v1` Release 的独立附件保存，每个文件小于 2 GiB。合计约 2.54 GB，不纳入 Git 历史，不依赖 Git LFS。

```bash
python3 tools/fetch_models.py --repo Hzzzch/iphone-gaussian-ios
```

脚本使用 GitHub CLI 凭据下载，然后按源码中固定清单核对大小和 SHA-256。Release 尚未发布时会报错。失败时保留已下载文件，重试请用新的 `--out` 目录。

有内网服务器权限时，也可直接获取完整模型：

```bash
python3 tools/fetch_models.py --host hezic@10.15.88.93
```

若由用户在 GitHub 网页创建 Release：标签用 `models-onnx-scaled-v1`，上传 `depth.onnx`、`features.onnx`、`head.onnx`、`manifest.json` 四个附件；不要上传 Android 的 QNN 缓存。

## 构建

先安装完整 Xcode，打开完成组件安装，选择支持目标手机的 iOS 平台。

```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -runFirstLaunch
bash tools/build_ios.sh
open GaussianIOS.xcworkspace
```

在 Xcode 设置签名 Team 后安装到 iPhone，将模型文件夹传到手机并在 Demo 内导入。首版尚未完成 Apple 平台编译和真机验收，详见 README 与 reports/status.json。
