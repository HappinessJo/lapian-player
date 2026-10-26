# 交接：0.5–2x 变速音高保持（SoundTouch）— 2026-10-26

- **状态**：实现、打包、安装验证完成（非门；不改首期其他范围）。
- **代码提交**：`7436abf feat(audio): pitch-preserving 0.5-2x playback via SoundTouch`
  （文档提交见 `git log` 顶部）。
- **上游**：[spec v0.4 增量](spec-v0.4-delta.md)、[spec.md](../spec.md) §3.4/§7.3/§9、
  [T3.3 交接](handoff-2026-10-20-t3.3.md)、[T3.4 交接](handoff-2026-10-20-t3.4.md)、
  [T3.3 测量](perf/T3.3-speed.md)。
- **用户边界**：0.25x 不强制音高保持；不能拖累整体流畅性、不能改坏。
- **用户体感**：本轮未收集新的体感；不写“验收通过”。

---

## 1. 一句话结论

0.5/0.75/1.25/1.5/2x 用 SoundTouch 2.3.3 做时间拉伸，**音高保持**；1x 完全旁路，
0.25x 保留原有变调；失败自动回退旧重采样。视频/UI/时间轴/在线会话一行未改。
同机 A/B 的拖拽/启动在噪声内，音频 underrun=0，A/V 仍走音频时钟。

## 2. 行为

| 速度 | 行为 |
|---|---|
| 0.25x | 原重采样变调（不要求音高保持） |
| 0.5x / 0.75x / 1.25x / 1.5x / 2x | SoundTouch tempo=倍速、pitch=1，音高保持 |
| 1.0x | 不经过 SoundTouch |

- 隐藏开关 `LAPIAN_PITCH_PRESERVE=0` 强制旧路径（A/B/回退）。
- 切速沿用“从当前媒体位置重启音频”的既有语义；不做播放中原地改 tempo。
- A/V 同步仍以 `IAudioClock × rate` 为媒体时钟；启动预填 30ms、目标缓冲 80ms 未改。

## 3. 代码/文件

| 文件 | 内容 |
|---|---|
| `src/audio/TimeStretch.{h,cpp}` | SoundTouch 包装（float32；音频线程；无锁/无 UI 调用） |
| `src/audio/AudioEngine.cpp` | 0.5–2x 走拉伸；1x/0.25x 原路径；EOF flush；失败回退；`LAPIAN_PITCH_PRESERVE` |
| `src/CMakeLists.txt` | SoundTouch 导入目标/链接/DLL 复制；测试目标依赖 |
| `tests/time_stretch_test.cpp` | 440Hz/长度/边界单测（16 项） |
| `tests/CMakeLists.txt` | 新测试目标（ctest 第 38 项） |
| `scripts/build-soundtouch.ps1` | 下载/校验/解压/构建 SoundTouch 2.3.3 DLL |
| `scripts/build-release.ps1` | 缺少 SoundTouch 时构建；DLL 复制到 dist |
| `scripts/package.ps1` | `SoundTouch.dll` 必需；`SoundTouch-COPYING.txt` 必需 |
| `licenses/SoundTouch-COPYING.txt` | LGPL-2.1 全文 |
| `licenses/THIRD-PARTY-NOTICES.md` / `SOURCE-OFFER.md` | SoundTouch 2.3.3 动态链接/源码说明 |
| `packaging/README.txt` | 用户可见的变速音频说明 |
| `spec.md` | v0.4：行为、F4-A5/A6/A11、§7.3、§9 |

- SoundTouch 源码在 `third_party/soundtouch`（gitignore）；由脚本按
  `https://www.surina.net/soundtouch/soundtouch-2.3.3.tar.gz`
  （SHA256 `43B23DFA…`）获取；`SoundTouch.dll` 68,096 B，
  SHA256 `B88698F3D42F864ADC0F7D1F8EF39F5BF566334F885EEE568C4F7D7D72E75013`。

## 4. 构建与产物（clean `-Clean -Package`）

| 项 | 结果 |
|---|---|
| 构建耗时 | **244.4s**（脚本内构建+dist 231.0s；ctest 66.96s） |
| ctest | **38/38**（37 既有 + `lapian_time_stretch_test`） |
| exe | 3,542,528 B，`E8149B92F0C4592C46766D9C1C9B6EFD66B65AC66AFCFCD20DD97F1FE6DDFB82` |
| dist | **26 文件 / 36,820,296 B / 35.11 MB**（含 `SoundTouch.dll`） |
| stage | 35 文件 / 36,961,262 B / 35.25 MB（含 licenses/README/PRIVACY） |
| setup | **12.02 MB**，`96C5A28B844BBF7B4AC2AB4AABFAF84DD3706F67E5BDA84A77CD4792202CDD8D` |
| zip | **14.67 MB**，`8040408034E291CBC3035EC24477FEFB77BFEF2830132B5B2560549A9C3F051F` |

- **build exe == dist exe == stage exe == 安装版 exe**（同 hash）。
- `check-imports.ps1`：21 个文件，无 QtNetwork/TLS/winhttp/wininet/urlmon；SoundTouch 仅
  KERNEL32/CRT。
- `package-report.txt` 记录 HEAD `d346eb0`：构建发生在代码提交 `7436abf` 之前，
  源码内容即 `7436abf`；报告里的 HEAD 字段是构建时的工作区 HEAD，不影响产物。

## 5. 安装验证

- setup 复制到 `%TEMP%\lapian-release-2026-10-26-pitch\`；静默
  `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCLOSEAPPLICATIONS /LOG=...`
  （`/NOCLOSEAPPLICATIONS` 避免触碰用户正在用的便携版）。
- 结果：**exit 0 / 2.59s**；`User privileges: None`、`Administrative install mode: No`、
  HKCU、`Need to restart Windows? No`。
- 安装目录：37 文件 / 43,963,886 B（35 负载 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`）；
  `lapian.exe` hash 与 dist 一致；`SoundTouch.dll` 68,096 B。
- Start 菜单/桌面快捷方式 target 均指向安装版 `lapian.exe`；`licenses\SoundTouch-COPYING.txt` 存在。
- `lapian-3d.exe`（`99084199…`）/`lapian-3c.exe`（`5863DD0A…`）未被删除。
- `%APPDATA%\拉片软件\settings.json`：安装前 mtime **18:21:05**（用户便携版改过
  volume/cache_root），安装时间 18:59:54，安装未改设置；本轮没有再做默认设置的
  “安装前后 hash 相同”断言（上一次发布收口已做）。

## 6. 安装版冒烟

| 项 | 结果 |
|---|---|
| `--banner-test` | **74/0** |
| `--key-test` | **77/0**（第三次；前两次受前台焦点影响，见 §7） |
| `--startup-test` | window_shown **765.62ms**、first_present **996.48ms** |
| `--drag-test` | P4 **64.3fps**、P6 p95 **31.2ms**、P7 0.1/13.8、P8 12.4fps/25 帧 |
| 安装版 2x 音频日志 | `audio device: 48000 Hz 2 ch ... pitch_preserve=1 stretch=1`；`audio start ... rate=2.00x pitch_preserve=1 stretch=1`；5s 内无 underrun |

## 7. 音高/性能证据（S1 + 安装版）

- `lapian_time_stretch_test`：**16/0**；输出/输入长度比 0.5x=2.0000、1x=1.0000、
  2x=0.5000；基频 0.5x **440.00Hz**、1x 438.33Hz、2x **440.00Hz**。
- SoundTouch CPU 基准（60s 48k stereo，单线程）：2x 126ms（播放时 **0.42%** 单核）、
  1x 138ms（0.23%）、0.5x 210ms（0.18%）。
- `lapian_audio_probe`：2x 3s→6.08s、1x→3.05s、0.5x→1.525s、0.25x→0.762s、
  2x+`LAPIAN_PITCH_PRESERVE=0`→6.06s；全部 `underruns=0`、`clock=audio`。
- 同机 A/B（S1 构建 vs 发布基线）：drag P4 **64.3 vs 64.9fps**、P6 p95
  **30.1 vs 30.0ms**、P7 14.3 vs 13.7、P8 12.5 vs 12.4；startup 792.6 vs 809.3ms。
- 结论：视频链路一行未改；增量只在音频线程，实测小于单核 0.5%。

## 8. 已知问题 / 残余

1. **`--key-test` 前台焦点 flake**：安装版第一次 76/1（`hold-2s-20frames` steps=1）、
   第二次 70/7（focus-hold/editor-focus 等），第三次 **77/0**。同一安装版、同一命令，
   失败项全与前台/焦点有关；不是音频改动路径。复跑可过；不写“永远稳定”。
2. **0.25x 仍变调**：用户明确不强制；0.25x 质量/行为与旧版一致。
3. **SoundTouch 0.5/2x 的瞬态/音质**：440Hz 与真实音乐抽听未发现明显问题；0.25x
   不进入拉伸器；没有做主观听音验收。
4. **只做了 3–5s 短时音频与 60s 离线 CPU 基准**：没有 10min/2h 连续变速 soak；
   P14/T7.7 仍按既有记录（资源阻塞）。
5. **没有变调/不变调 UI 开关**（不改 settings schema）；需要时另立任务。
6. **setup/zip hash 已变**：上一版发布记录
   [release-2026-10-26-ui-v2-hotfix.md](release-2026-10-26-ui-v2-hotfix.md) 的
   `957EC30A…`/`76F9B6FA…` 是音高保持之前的包；本版以本文 §4 为准。

## 9. 回退

- 代码：`git revert 7436abf`（TimeStretch/AudioEngine/CMake/打包/许可一起回退）。
- 运行期：`LAPIAN_PITCH_PRESERVE=0` 立即回旧重采样（无需重启构建）。
- 分发：保留上一版 setup `957EC30A…`（`build\pre-close-baseline`/历史产物）可回滚；
  本版安装版覆盖了 C: 的 `lapian.exe`，回滚需重装旧 setup。
- 0.25x/1x 原路径未改，回退风险低。

## 10. 复现

```powershell
# 依赖
powershell -ExecutionPolicy Bypass -File scripts\build-soundtouch.ps1

# 构建 + 单测 + 打包
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Clean -Package

# 音高单测 / 基准（独立构建）
ctest --test-dir build\release -C Release -R lapian_time_stretch_test --output-on-failure
& build\pitch-prep\standalone\build\Release\pitch_bench.exe

# 安装版冒烟（Medium）
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
& $exe --banner-test --file 'media\测试.mp4' --banner-tag pitch --settings-path build\pitch-settings
& $exe --key-test --file 'media\测试.mp4' --settings-path build\pitch-settings
& $exe --drag-test --file 'media\测试.mp4' --settings-path build\pitch-settings --drag-samples 20
```
