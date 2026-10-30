#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/macos-release}"

for package in cmake ninja qt sound-touch curl; do
  if ! brew list --versions "$package" >/dev/null 2>&1; then
    echo "缺少依赖：brew install $package" >&2
    exit 1
  fi
done

QT_PREFIX="${QT_PREFIX:-$(brew --prefix qt)}"
FFMPEG_ROOT="${FFMPEG_ROOT:-$ROOT/third_party/ffmpeg/macos/ffmpeg-${FFMPEG_VERSION:-9.0.2}-minimal}"
if [[ ! -f "$FFMPEG_ROOT/include/libavutil/ffversion.h" ]]; then
  "$ROOT/scripts/build-ffmpeg-macos.sh"
fi
SOUNDTOUCH_ROOT="${SOUNDTOUCH_ROOT:-$(brew --prefix sound-touch)}"
CURL_ROOT="${CURL_ROOT:-$(brew --prefix curl)}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DFFMPEG_ROOT="$FFMPEG_ROOT" \
  -DSOUNDTOUCH_ROOT="$SOUNDTOUCH_ROOT" \
  -DCURL_ROOT="$CURL_ROOT"

cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure
