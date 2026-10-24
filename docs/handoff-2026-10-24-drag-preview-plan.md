# 交接：拖动预览 v2 规划（DP 计划）

- **日期**：2026-10-24
- **状态**：**规划完成，未实施**。下一步从 **DP-1 硬件预览** 开始。
- **基线**：代码 `079f0dd`（本地快拖热修）；DP-0 规划/交接已提交（以 `git log -1` 为准）；工作区干净；dist **34.48MB/25**。
- **计划**：[plan-drag-preview-v2.md](plan-drag-preview-v2.md)
- **任务表**：[tasks-drag-preview-v2.md](tasks-drag-preview-v2.md)
- **上一轮证据**：[local-drag-fast-2026-10-24.md](perf/local-drag-fast-2026-10-24.md)
- **上一轮交接**：[handoff-2026-10-24-drag-fast.md](handoff-2026-10-24-drag-fast.md)
- **下一会话提示词**：[next-session-prompt-dp1.md](next-session-prompt-dp1.md)

---

## 1. 当前完成到哪

`079f0dd` 已完成“不冻住”热修：

- `PreviewDecoder` 的 `generation`（目标版本）与 `session`（拖动会话）分离；
- 正向拖动每解一帧追最新目标，发布中间帧，不因目标变化中断；
- 反向拖动先解完当前目标再取最新，允许“比当前目标晚、但比上一张已显示帧
  更早”的粗帧；
- 正向连续窗口 2s→5s；刚 seek 的远跳继续解完；
- 新增 `--drag-fast`（正/负 step）真实路径探针。

实测 `media\测试.mp4`（1080p H.264 30fps、4573 帧、GOP 30 帧）：

| 场景 | 修改前 | 修改后 |
|---|---|---|
| +60 帧/16ms | 28.6fps / 空档 93.8ms | **60.0–61.0fps / 32ms** |
| +120 / +300 | — | 59–60fps / 32ms |
| 反向 −60 | 约 23fps / >700ms | **42.7fps / 48.5ms** |
| 反向 −20 | — | **11.5fps / 191.8ms（残余）** |
| P4 / P6 | 55.4fps / — | **61.3fps / 17.1ms** |
| P7 / P8 / P9 | — | 0.0/9.6ms、12.7fps、33.3ms |

回归：`ctest` **36/36**、key **77/0**、T8.6.1b **35/0**、startup/step1080
基准 PASS、`dist` 34.48MB/25。

**重要口径**：上述 fps 是 Present 帧率，不代表“显示帧 == 鼠标目标帧”。
+60 帧/16ms 的目标速度远高于软件解码吞吐；60fps 只说明画面在动，内容仍可能
落后目标。下一阶段必须测 `target_lag`。

## 2. 当前架构/瓶颈

```
MainWindow
  ├─ 主播放：MediaSession + 硬件 VideoDecoder → GfxWidget/VideoRenderer
  └─ 拖动预览：PreviewDecoder（独立线程，软件 VideoDecoder）
        seek 关键帧 → 解码 → sws 到 ≤1280 NV12 → 32 帧缓存
        → TakeLatestFrame → GfxWidget
```

- 预览 `decoder.Open(..., false)`：**软件**；主播放的硬解不覆盖预览。
- 预览单线程，seek 同步；软件 seek p50 26.5ms/p95 **77.9ms**，
  硬件 p50 12.8ms/p95 **29.4ms**；60Hz 预算 16.7ms。
- 反向不能倒放：每个更早目标都要 seek 到更早关键帧再向前解；32 帧缓存只覆盖
  刚拖过的一小段。
- `media\测试.mp4` GOP=30 帧（1s）；这已经是短 GOP。
- 4K/HEVC 仍软解；不承诺。

## 3. 为什么规划要分 DP-1/2/3/4

- **DP-1 硬件预览**：降低单次 seek/解码成本；但反向每步仍 seek，硬件 p95
  29.4ms 也追不上 16ms，所以单靠 DP-1 不能“随心所欲”。
- **DP-2 缓存/预取/粗帧**：反向优先命中已解帧；远跳先出缩略图；解决
  “反向卡”和“超大幅度空等”。
- **DP-3 策略/诊断**：快拖近似、慢拖精确、松手精确；测 `target_lag`，
  不再只看 fps。
- **DP-4 集成**：全量回归/dist/交接。

不做全片缓存：1080p NV12 约 3.1MB/帧，2h 30fps ≈670GB；720p 也约 300GB。
缓存必须有界；远跳靠低分辨率缩略图，不靠全片。

## 4. 已拍板/边界

- 1080p 先达标；4K/HEVC best-effort，只记录，不设门。
- 拖动中允许近似；松手 `GoToFrame` 精确，P6 ≤150ms 不变。
- 不改 M1/M2/P4/P7 门/数字、OB 样本、`lapian-cache-1` schema、默认
  AVC≤1080p。
- 不改 `OnlinePreviewDecoder`/T8.6.1b；在线预览仍走 T8.6.1b。
- 不引入新播放器/框架；只动预览子系统。
- 预览缓存硬上限：主缓存 ≤256MB；缩略图 ≤64MB；超限淘汰，不增长。
- 任何主播放/在线回归 → 回滚 DP 改动。

## 5. 代码地图（DP-1 重点）

| 位置 | 作用 |
|---|---|
| `src/media/PreviewDecoder.h/.cpp` | 预览线程、软件解码、32 帧缓存、session/generation |
| `src/media/VideoDecoder.h/.cpp` | `OpenWithD3D11Device`、硬件帧、seek |
| `src/app/MainWindow.cpp` | `OnScrubStarted/OnScrubTargetChanged/OnScrubFinished`、`OnPreviewTick`、`preview_` 配置 |
| `src/app/GfxWidget.h/.cpp` | `d3d_device()`、`SetVideoFrame` |
| `src/platform/gfx/VideoRenderer.cpp` | 同设备硬件帧 `UploadHardwareFrame` |
| `src/app/DragTestRunner.*` | `--drag-fast` 探针 |
| `tests/preview_decoder_test.cpp` | 软件预览回归 |

## 6. DP-1 第一件事

先做**最小 spike**，不要直接改产品：

1. 在预览线程用 `VideoDecoder::OpenWithD3D11Device(path, stream,
   gfx_->d3d_device(), true, &err)` 打开；
2. 解一帧，确认 `HardwareActive()==true`；
3. 把该硬件帧交给 `GfxWidget::SetVideoFrame`，确认同设备纹理能显示、不黑；
4. 再决定是否正式改 `PreviewDecoder::Configure` 接口。
5. 失败立即回退软件，记录原因；不写“假硬解”。

## 7. 测试/停止条件

- 基线复跑：`--drag-fast ±60`、`--drag-test`、`ctest` 36/36、key 77/0。
- DP-1 退出：`hw_active=1`（开发机）、正向不低于 60fps/≤50ms、反向 −60
  ≥42fps/≤80ms、无黑帧/崩溃、软件回退可用、T8.6.1b 35/0。
- 如果共享设备导致黑帧/崩溃/设备丢失不可解：保留软件路径，DP-1 只交付
  回退+诊断，不强行硬解。
- 4K 只记录；不因 4K 不达标阻塞 1080p。

## 8. 下一步

打开新会话，使用 `docs/next-session-prompt-dp1.md` 的提示词；先读本文件、
计划、任务表，再动代码。不要从聊天上下文猜。
