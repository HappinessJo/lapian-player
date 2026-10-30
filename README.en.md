# Lapian Player

[中文 README](README.md) · [Upstream / Original project](https://github.com/HappinessJo/lapian-player) · [Releases](https://github.com/luoboruodai/lapian-player/releases)

**A frame-accurate review tool for MAD/PV work, now with a native macOS Apple Silicon build.**

Local frame stepping · Bilibili playback and scrub preview · A-B looping · Marks, notes and snapshots

[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-macOS%20arm64%20%2B%20Windows%20x64-blue.svg)](#platforms-and-downloads)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](#building-from-source)
[![Qt](https://img.shields.io/badge/Qt-6.12.0-41CD52.svg)](#building-from-source)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-9.0.2%20LGPL-007808.svg)](licenses/THIRD-PARTY-NOTICES.md)

Break a MAD/PV down to individual frames: inspect cuts, transitions, composition and beats,
then keep a record of what you find.

This repository is a fork of [HappinessJo/lapian-player](https://github.com/HappinessJo/lapian-player).
**HappinessJo is the original author.** This fork adds native macOS support, build and release
automation, and Chinese/English documentation.

## Platforms and downloads

| Platform | Delivery | Notes |
|---|---|---|
| macOS Apple Silicon | `Lapian-*-macos-arm64.dmg` / `.zip` | Native arm64, built and tested on a real Mac |
| Windows 10/11 x64 | Upstream installer and portable build | Keeps the original D3D11 / WinHTTP / WASAPI implementation |

macOS installation:

1. Open the DMG and drag `Lapian.app` to Applications, or extract the ZIP somewhere writable.
2. The package is **ad-hoc signed and not notarized by Apple**. Verify the SHA-256 in the
   release, then right-click the app in Finder and choose Open. Do not disable system-wide
   security protections.
3. Intel Macs are not supported, and no Universal Binary is included. Other macOS versions
   and hardware configurations have not been verified individually.

GitHub's automatic **Source code** archives contain source, not an installable app.
Use the DMG or ZIP asset instead.

## Features

### Local video

- Step one frame with `PgUp` / `PgDn` or arrow keys; jump by frame number or `HH:MM:SS:FF`.
- Scrub the timeline with preview and exact seek on release.
- Playback from 0.25x to 2x, A-B looping and single-frame hold.
- Audio waveform, volume, mute and fullscreen.
- Marks, notes, source-resolution PNG snapshots and sidecar persistence.

### Bilibili online

- Open a BV ID or URL, play while caching, scrub and frame-step.
- DASH quality selection, cache hits and per-video A/B workspace memory.
- Official QR login; SESSDATA is kept in the platform credential store.
- Public content and user-supplied credentials only: no payment, DRM or regional bypass.

## Quick start

1. Open a local file with `Cmd+O` (the UI also shows `Ctrl+O`), or a Bilibili URL with
   `Cmd/Ctrl+U`.
2. `Space`: play/pause. `PgUp/PgDn`: previous/next frame.
3. `I` / `O`: set A/B. `L`: toggle loop. `Shift+L`: clear the range.
4. `W`: waveform. `M`: mute. `S`: source-resolution snapshot. `Ctrl/Cmd+M`: add a mark.
5. `[` / `]`: playback speed. `F` or double-click: fullscreen.

Data locations:

- Marks and notes default to `<video>.lapian.json` next to the video, with fallback to
  `~/Library/Application Support/拉片软件/marks` when the directory is read-only.
- Online cache: `~/Library/Caches/拉片软件/cache`, configurable and clearable in Settings.
- Credentials: macOS Keychain, never plaintext in the marker file.
- Deleting the app does not delete settings, records, snapshots or cache.

## The macOS port

- **Video**: FFmpeg software decoding + Qt `QImage` / `QPainter`, including rotation and resizing.
- **Audio**: CoreAudio / AudioUnit, preserving the audio clock, tempo and waveform paths.
- **Networking**: libcurl with HTTPS verification, Range resume, cancellation and no redirects.
- **Credentials**: macOS Keychain; SESSDATA is never sent to a CDN.
- **Files and cache**: POSIX I/O and shared read locks so active cache files are not evicted.
- **Packaging**: CMake + Ninja + macdeployqt, producing a self-contained app, DMG and ZIP.

See [ADR-008](docs/adr/ADR-008-macos-port.md) for the platform decision record.

## Building from source

Requirements: Homebrew, Apple Clang, CMake, Ninja, Qt, SoundTouch and libcurl,
plus Python 3 for package validation. This release uses Apple Clang 21, Qt 6.12.0,
FFmpeg 9.0.2, SoundTouch 2.4.1 and libcurl 8.23.0. The build script compiles a minimal
LGPL FFmpeg from official sources rather than using Homebrew's GPL-enabled build.

```bash
git clone https://github.com/luoboruodai/lapian-player.git
cd lapian-player
brew install cmake ninja qt sound-touch curl python

./scripts/build-macos.sh
./scripts/package-macos.sh
```

Artifacts are written to `build/macos-dist/`. `BUILD_DIR`, `VERSION` and `MACDEPLOYQT`
can be overridden through environment variables. Before publishing, run the tests and
inspect dependency paths, the signature and a mounted DMG.

For the original Windows / Visual Studio 2022 build, see the
[upstream repository](https://github.com/HappinessJo/lapian-player) and [docs/deps.md](docs/deps.md).

## Verification and limitations

Verified on macOS: real-window playback, forward/backward frame stepping, marks, 720p PNG
snapshots, and **35/35 CTest tests** (four optional VFR, 10-bit and Windows-specific cases skipped).
The anonymous public-video API probe passed **38/38 checks**, and the DASH end-to-end probe
passed **25/25 checks**, covering real download, first frame, stepping, seeks, waveform,
marks, an 852×480 snapshot and close.
Warm-only startup preheating covers `[frame−61, frame+10]`.
See the [release notes](docs/release-2026-10-30-macos-arm64.md).

- Apple Silicon only; Intel, other GPUs and every macOS version are not covered.
- Software decoding/rendering does not inherit the Windows D3D11 performance targets;
  4K/60fps is not guaranteed.
- Packages are not notarized and have no Developer ID signature.
- Online quality and availability depend on Bilibili, the network and account permissions.
  Real QR login still needs confirmation with a user's phone.
- Optional VFR, 10-bit and Windows-only probes are not claimed as verified.
- No claim is made that every codec, environment or long-running session is defect-free.

## Contributing and attribution

Original author: **HappinessJo**. This fork's macOS port is based on the original MIT-licensed
project. Issues and PRs are welcome for real-device results, reproducible bugs, build fixes
and bilingual documentation.

Workflow: fork → branch → change → `ctest` → PR. Never commit `build/`, `third_party/`,
video assets, accounts, cookies or credentials.

## Disclaimer

For personal study, technical research and frame-by-frame review only. The project does not
provide, host or redistribute video content, accounts or cookies. When using online features,
follow Bilibili's terms and applicable copyright law. Do not use it for bulk downloading,
redistribution or infringement.

## License

Released under **MIT**, see [LICENSE](LICENSE), retaining HappinessJo's original copyright.
Earlier GPL grants for v0.1.0 and earlier versions remain valid.

Third-party licenses still apply: Qt (LGPL-3.0), FFmpeg (LGPL-2.1+), SoundTouch (LGPL-2.1),
libcurl (curl/X11) and qrcodegen (MIT). Keep `licenses/`, `licenses/SOURCE-OFFER.md` and
`licenses/SOURCE-OFFER-MAC.md` with binary distributions.
