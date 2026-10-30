#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/macos-release}"
FFMPEG_ROOT="${FFMPEG_ROOT:-$ROOT/third_party/ffmpeg/macos/ffmpeg-${FFMPEG_VERSION:-9.0.2}-minimal}"
WORK="$ROOT/build/macos-package"
OUT="$ROOT/build/macos-dist"
CANDIDATE="$WORK/output"
VERSION="${VERSION:-0.1.0-macos.1}"
APP="$WORK/Lapian.app"
DMG_NAME="Lapian-${VERSION}-macos-arm64"
MACDEPLOYQT="${MACDEPLOYQT:-$(brew --prefix qtbase)/bin/macdeployqt}"

if [[ ! -x "$MACDEPLOYQT" ]]; then
  echo "找不到 macdeployqt：$MACDEPLOYQT" >&2
  exit 1
fi
if [[ ! -d "$BUILD_DIR/bin/lapian.app" ]]; then
  echo "请先运行 scripts/build-macos.sh" >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "打包检查需要 python3" >&2
  exit 1
fi

rm -rf "$WORK"
mkdir -p "$WORK" "$CANDIDATE" "$OUT"
cp -R "$BUILD_DIR/bin/lapian.app" "$APP"
"$MACDEPLOYQT" "$APP" -always-overwrite -verbose=1 -libpath="$(brew --prefix)/lib"
rm -rf "$APP/Contents/Frameworks/QtQml.framework" \
  "$APP/Contents/Frameworks/QtQmlMeta.framework" \
  "$APP/Contents/Frameworks/QtQmlModels.framework" \
  "$APP/Contents/Frameworks/QtQmlWorkerScript.framework" \
  "$APP/Contents/Frameworks/QtQuick.framework" \
  "$APP/Contents/Frameworks/QtNetwork.framework"
rm -f "$APP/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib" \
  "$APP/Contents/PlugIns/iconengines/libqsvgicon.dylib" \
  "$APP/Contents/PlugIns/imageformats/libqpdf.dylib" \
  "$APP/Contents/PlugIns/imageformats/libqsvg.dylib"
mkdir -p "$APP/Contents/Resources/licenses"
cp -R "$ROOT/licenses/." "$APP/Contents/Resources/licenses/"
cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
cp "$ROOT/packaging/PRIVACY.txt" "$APP/Contents/Resources/PRIVACY.txt"
cp "$ROOT/packaging/README-mac.txt" "$APP/Contents/Resources/README.txt"
cp "$ROOT/docs/adr/ADR-008-macos-port.md" "$APP/Contents/Resources/MACOS-PORT.md"
cp "$FFMPEG_ROOT/FFMPEG-LICENSE.md" "$APP/Contents/Resources/licenses/FFmpeg-LICENSE.md"
mkdir -p "$APP/Contents/Resources/licenses/homebrew"
copy_license() {
  cp "$(brew --prefix "$1")/$2" "$APP/Contents/Resources/licenses/homebrew/$3"
}
copy_license openssl@4 LICENSE.txt openssl-LICENSE.txt
copy_license libnghttp2 COPYING libnghttp2-COPYING
copy_license libnghttp3 COPYING libnghttp3-COPYING
copy_license libngtcp2 COPYING libngtcp2-COPYING
copy_license libpsl COPYING libpsl-COPYING
copy_license libssh2 COPYING libssh2-COPYING
copy_license zstd LICENSE zstd-LICENSE
copy_license brotli LICENSE brotli-LICENSE
copy_license dbus COPYING dbus-COPYING
copy_license glib LGPL-2.1-or-later.txt glib-LICENSE.txt
copy_license pcre2 LICENCE.md pcre2-LICENSE.md
copy_license harfbuzz COPYING harfbuzz-COPYING
copy_license freetype LICENSE.TXT freetype-LICENSE.txt
copy_license libpng LICENSE libpng-LICENSE
copy_license double-conversion LICENSE double-conversion-LICENSE
copy_license md4c LICENSE.md md4c-LICENSE.md
copy_license graphite2 LICENSE graphite2-LICENSE
copy_license icu4c@79 LICENSE icu4c-LICENSE
copy_license gettext COPYING gettext-COPYING
copy_license jpeg-turbo LICENSE.md jpeg-turbo-LICENSE.md
copy_license libtiff LICENSE.md libtiff-LICENSE.md
copy_license webp COPYING webp-COPYING
copy_license xz COPYING.LGPLv2.1 xz-LGPL-2.1.txt
python3 - "$VERSION" "$ROOT/packaging/README-mac.txt" <<'PY'
from pathlib import Path
import sys
if sys.argv[1] not in Path(sys.argv[2]).read_text(encoding="utf-8"):
    raise SystemExit("离线指南与发布版本不一致")
PY
python3 "$ROOT/scripts/check-macos-bundle.py" "$APP"

codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
BASE_VERSION="${VERSION%%-*}"
if [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")" != "$BASE_VERSION" ]]; then
  echo "App 版本与发布版本不一致" >&2
  exit 1
fi

mkdir -p "$WORK/dmg-root"
cp -R "$APP" "$WORK/dmg-root/Lapian.app"
ln -s /Applications "$WORK/dmg-root/Applications"
hdiutil create -fs HFS+ -format UDZO -volname Lapian \
  -srcfolder "$WORK/dmg-root" -ov "$CANDIDATE/${DMG_NAME}.dmg"
hdiutil imageinfo "$CANDIDATE/${DMG_NAME}.dmg" >/dev/null

(
  cd "$WORK/dmg-root"
  COPYFILE_DISABLE=1 zip -q -r -X "$CANDIDATE/${DMG_NAME}.zip" Lapian.app
)
unzip -t "$CANDIDATE/${DMG_NAME}.zip" >/dev/null
python3 - "$CANDIDATE/${DMG_NAME}.zip" <<'PY'
from pathlib import Path
from zipfile import ZipFile
import sys
with ZipFile(sys.argv[1]) as archive:
    members = archive.namelist()
    for required in ("LICENSE", "PRIVACY.txt", "README.txt", "MACOS-PORT.md"):
        if f"Lapian.app/Contents/Resources/{required}" not in members:
            raise SystemExit(f"ZIP 缺少 {required}")
    if not any(name.startswith("Lapian.app/Contents/Resources/licenses/") for name in members):
        raise SystemExit("ZIP 缺少第三方许可")
    for name in members:
        if name.startswith(("/", "__MACOSX/")) or "/._" in name or name.endswith("/.DS_Store"):
            raise SystemExit("ZIP 包含非分发成员")
print(f"ZIP members checked: {len(members)}")
PY
(
  cd "$CANDIDATE"
  shasum -a 256 "${DMG_NAME}.dmg" "${DMG_NAME}.zip" > SHA256SUMS
)

mkdir -p "$WORK/mount"
trap 'hdiutil detach "$WORK/mount" >/dev/null 2>&1 || true' EXIT
hdiutil attach -nobrowse -readonly -mountpoint "$WORK/mount" "$CANDIDATE/${DMG_NAME}.dmg" >/dev/null
python3 "$ROOT/scripts/check-macos-bundle.py" "$WORK/mount/Lapian.app"
codesign --verify --deep --strict "$WORK/mount/Lapian.app"
hdiutil detach "$WORK/mount" >/dev/null
trap - EXIT

for suffix in dmg zip; do
  cp "$CANDIDATE/${DMG_NAME}.$suffix" "$OUT/.${DMG_NAME}.$suffix.candidate"
  mv "$OUT/.${DMG_NAME}.$suffix.candidate" "$OUT/${DMG_NAME}.$suffix"
done
cp "$CANDIDATE/SHA256SUMS" "$OUT/.SHA256SUMS.candidate"
mv "$OUT/.SHA256SUMS.candidate" "$OUT/SHA256SUMS"
printf '%s\n' "$OUT/${DMG_NAME}.dmg" "$OUT/${DMG_NAME}.zip" "$OUT/SHA256SUMS"
