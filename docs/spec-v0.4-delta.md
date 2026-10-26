# spec v0.4 增量：0.5–2x 变速音高保持（2026-10-26）

> 本文件是 [spec.md](../spec.md) v0.4 的补充说明；验收口径以 spec.md 正文
> §3.4 / §7.3 / §9 为准。v0.4 只改变速音频语义，不改首期其他范围。

## 1. 行为

| 速度 | 音频行为 |
|---|---|
| 0.25x | 保留原有重采样变调（0.25x 不要求音高保持） |
| 0.5x / 0.75x / 1.25x / 1.5x / 2.0x | **保持音高**（SoundTouch 时间拉伸，tempo = 播放倍速） |
| 1.0x | 完全旁路，不经过时间拉伸 |

- 变速时仍以音频时钟为主；画面、帧号、时间码、循环区间与当前帧不变。
- A/V 偏差仍要求 ≤1 帧（F4-A6）。
- 失败回退：SoundTouch 初始化/运行异常时自动回退旧重采样，不阻塞视频；
  隐藏诊断开关 `LAPIAN_PITCH_PRESERVE=0` 可强制旧路径。
- 不改 UI/键位/settings schema/现有性能门。

## 2. 验收条款

- **F4-A5** 0.25x：画面与音频同步变慢；音频允许变调；暂停后帧号准确；步进 1 帧。
- **F4-A6** 2x：播放不中断；音画偏差 ≤1 帧；**440Hz 测试音基频保持 440±5Hz**；
  时间轴与帧号不漂移。
- **F4-A11** 0.5x/0.75x/1.25x/1.5x/2x：440Hz 基频 440±5Hz；1x 旁路；0.25x 允许变调。

## 3. 实现与证据（2026-10-26）

- 依赖：SoundTouch 2.3.3（LGPL-2.1，动态 `SoundTouch.dll`，68,096 B）；
  构建脚本 `scripts/build-soundtouch.ps1`。
- 实现：`src/audio/TimeStretch.*`（音频线程包装）+ `AudioEngine` 分支；
  1x/0.25x 原路径；失败回退。
- 单测：`lapian_time_stretch_test`（ctest 38/38）——0.5x **440.00Hz**、
  1x 438.33Hz、2x **440.00Hz**，长度比 2.0/1.0/0.5。
- CPU 基准（60s 48k stereo，单线程处理）：2x 126ms（播放时 **0.42%** 单核）、
  1x 138ms（0.23%）、0.5x 210ms（0.18%）。
- 实测：`lapian_audio_probe` 2x/1x/0.5x/0.25x/回退 2x 均 `underruns=0`、
  `clock=audio`；安装版 2x 日志 `pitch_preserve=1 stretch=1`。
- 不拖累视频：S1 构建与发布基线同机 `--drag-test` P4 64.3 vs 64.9fps、
  P6 p95 30.1 vs 30.0ms；startup 792.6 vs 809.3ms（同机噪声内）。
- 详细数字/命令见 [handoff-2026-10-26-pitch-preserve.md](handoff-2026-10-26-pitch-preserve.md)。

## 4. 不包含

- 0.25x 音高保持（用户明确不强制）。
- 变调/不变调 UI 开关（不改 settings schema）。
- 视频/UI/时间轴/在线会话/缓存语义改动。
- 正式 T7.7/P14/MVP 验收（资源仍缺，见 spec v0.3 记录）。
