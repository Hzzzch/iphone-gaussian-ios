#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'iOS build requires macOS and Xcode; Windows/Linux cannot produce a validated IPA.' >&2
  exit 2
fi
command -v xcodegen >/dev/null
command -v pod >/dev/null
xcodebuild -version
xcodegen generate
pod install
# Compile for a physical iPhone without signing. Signing is configured in Xcode.
xcodebuild -workspace GaussianIOS.xcworkspace -scheme GaussianIOS \
  -configuration Release -destination 'generic/platform=iOS' \
  -derivedDataPath build/DerivedData CODE_SIGNING_ALLOWED=NO build
