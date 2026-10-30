#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${FFMPEG_VERSION:-9.0.2}"
SHA256="8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
JOBS="${JOBS:-4}"
BASE="$ROOT/third_party/ffmpeg"
ARCHIVE="$BASE/sources/ffmpeg-${VERSION}.tar.xz"
SOURCE="$BASE/sources/ffmpeg-${VERSION}"
BUILD="$BASE/macos/build-${VERSION}"
INSTALL="$BASE/macos/ffmpeg-${VERSION}-minimal"

if [[ "$(uname -m)" != "arm64" ]]; then
  echo "当前发布脚本仅支持 Apple Silicon（arm64）" >&2
  exit 1
fi
for tool in clang make curl tar shasum; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "缺少构建工具：$tool" >&2
    exit 1
  fi
done

mkdir -p "$BASE/sources"
if [[ ! -f "$ARCHIVE" ]]; then
  curl --fail --location --proto '=https' --tlsv1.2 \
    --output "$ARCHIVE" "https://ffmpeg.org/releases/ffmpeg-${VERSION}.tar.xz"
fi
ACTUAL="$(shasum -a 256 "$ARCHIVE" | cut -d ' ' -f 1)"
if [[ "$ACTUAL" != "$SHA256" ]]; then
  echo "FFmpeg 源码归档 SHA-256 不匹配：$ARCHIVE" >&2
  exit 1
fi
if [[ ! -f "$SOURCE/configure" ]]; then
  tar -xf "$ARCHIVE" -C "$BASE/sources"
fi

rm -rf "$BUILD" "$INSTALL"
mkdir -p "$BUILD" "$INSTALL"
cd "$BUILD"
"$SOURCE/configure" \
  --prefix="$INSTALL" \
  --arch=arm64 \
  --target-os=darwin \
  --cc=clang \
  --disable-everything \
  --disable-autodetect \
  --disable-programs \
  --disable-doc \
  --disable-network \
  --disable-avdevice \
  --disable-avfilter \
  --disable-debug \
  --disable-gpl \
  --disable-version3 \
  --disable-nonfree \
  --enable-shared \
  --disable-static \
  --enable-pthreads \
  --enable-pic \
  --enable-protocol=file,pipe \
  --enable-demuxer=mov,matroska \
  --enable-decoder=h264,hevc,vp9,av1,aac,mp3,opus,pcm_s16le,pcm_s24le,pcm_f32le,pcm_s16be,pcm_s24be,pcm_u8 \
  --enable-parser=h264,hevc,vp9,av1,aac,opus \
  --enable-swscale \
  --enable-swresample
make -j"$JOBS"
make install
cp "$BUILD/ffbuild/config.log" "$INSTALL/build-config.log"
cp "$SOURCE/LICENSE.md" "$INSTALL/FFMPEG-LICENSE.md"
printf '%s\n' "$INSTALL"
