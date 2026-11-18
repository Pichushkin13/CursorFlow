#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "$(uname -s)" != "Darwin" ]; then
  echo 'Build on macOS with Apple Command Line Tools installed.' >&2
  exit 1
fi
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp/}CursorFlow-build.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
APP="$BUILD_DIR/CursorFlow.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" dist
xcrun clang -arch arm64 -mmacosx-version-min=11.0 -O2 -Wall -Wextra -Werror \
  src/CursorFlow.c -o "$APP/Contents/MacOS/CursorFlow"
cp assets/Info.plist "$APP/Contents/Info.plist"
cp assets/AppIcon.icns assets/TrayIcon.png "$APP/Contents/Resources/"
printf 'APPL????' > "$APP/Contents/PkgInfo"
codesign --force --sign - --timestamp=none "$APP"
codesign --verify --strict "$APP"
STAGE="$BUILD_DIR/disk"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp README.md "$STAGE/README.md"
hdiutil create -volname CursorFlow -srcfolder "$STAGE" -ov -format UDZO dist/CursorFlow_AppleSilicon.dmg
printf 'Built: dist/CursorFlow_AppleSilicon.dmg\n'
