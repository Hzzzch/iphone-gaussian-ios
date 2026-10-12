# GitHub 与 Mac 接续开发

源码已上传到 [Hzzzch/iphone-gaussian-ios](https://github.com/Hzzzch/iphone-gaussian-ios)，分支为 `main`。**模型 Release 尚未上传**，目前请使用下方服务器下载方式。

## Mac 登录与获取源码

Mac 的 SSH 密钥独立于 Windows，需要在 Mac 上另行配置或使用 GitHub CLI 的 HTTPS 登录。私钥不要放进项目。

```bash
brew install xcodegen cocoapods python
ssh -T git@github.com
git clone git@github.com:Hzzzch/iphone-gaussian-ios.git
cd iphone-gaussian-ios
```

上面的克隆方式要求 Mac 已配置自己的 GitHub SSH 密钥。也可安装 `gh`，使用 `gh auth login --hostname github.com --git-protocol https --web` 完成网页登录，再用 `gh repo clone Hzzzch/iphone-gaussian-ios` 克隆。私有 Release 下载需要 GitHub CLI 的 API 授权，SSH 不能替代它。

## 模型

三个完整 ONNX 合计约 2.54 GB，不纳入 Git 历史，不依赖 Git LFS。当前可从服务器获取，Mac 需要能访问内网并具备 SSH 登录权限：

```bash
python3 tools/fetch_models.py --host hezic@10.15.88.93
```

脚本按源码中固定清单核对大小和 SHA-256。失败时保留已下载文件，重试请用新的 `--out` 目录。

未来发布 `models-onnx-scaled-v1` Release 后，可改为下列方式。目前该 Release 尚未上传，不要提前执行：

```bash
python3 tools/fetch_models.py --repo Hzzzch/iphone-gaussian-ios
```

若由用户在 GitHub 网页创建 Release：标签用 `models-onnx-scaled-v1`，上传 `depth.onnx`、`features.onnx`、`head.onnx`、`manifest.json` 四个附件；不要上传 Android 的 QNN 缓存。

已校验的本地附件目录为 Windows 的 `D:\skd\codec\Server\iphone-ios\models\onnx-scaled-v1`。每个模型均小于 2 GiB，可分别作为附件上传。

## 构建

先安装完整 Xcode，打开完成组件安装，选择支持目标手机的 iOS 平台。

```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -runFirstLaunch
bash tools/build_ios.sh
open GaussianIOS.xcworkspace
```

在 Xcode 设置签名 Team 后安装到 iPhone，将模型文件夹传到手机并在 Demo 内导入。首版尚未完成 Apple 平台编译和真机验收，详见 README 与 reports/status.json。
