# 交接：本地拖动预览快速大跳热修（2026-10-24）

- **状态**：**完成，用户反馈热修**。非门、非 MVP 验收；不改 P4/P7 数字、
  M1/M2 门、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p。
- **基线**：T8.15 提交 `1014b20`；`ctest` 36/36、key 77/0、longpress 54/0。
- **问题**：快速拖动时间轴时画面卡住；本地 1080p 也复现。用户对比 AE（缓存好时）
  与 Windows 电影和电视的跟手拖动。
- **证据**：[docs/perf/local-drag-fast-2026-10-24.md](perf/local-drag-fast-2026-10-24.md)。

## 1. 根因

`PreviewDecoder` 是独立软件预览解码器：

1. 每个 `UpdateTarget` 都让 generation+1；解码线程下一帧发现过期就放弃。
2. 快速大跳反复“seek 关键帧→解一帧→被取消”，画面整段空住。
3. 反向拖动只发布“正好等于目标”的帧，关键帧到目标之间不显示。
4. 现有 P4 只测每 16ms +3 帧的小步，覆盖不到快速大跳。

实测 `media\测试.mp4`：1080p H.264 30fps、4573 帧、GOP 30 帧（1s）；软件
seek p50 26.5ms/p95 77.9ms、硬件 12.8/29.4ms。修复前快拖 +60 帧/16ms：
28.6fps、最大空档 93.8ms。

## 2. 改动

| 文件 | 改动 |
|---|---|
| `src/media/PreviewDecoder.cpp` | `generation`（目标版本）与 `session`（拖动会话）分离；正向追最新目标、发布中间帧不中断；反向坚持解完当前目标再取最新；反向粗帧仅在“比上一张更早”时接受；32 帧预览缓存；正向连续窗口 2s→5s；刚 seek 的远跳不重复 seek |
| `src/app/DragTestRunner.*` | 新增 `--drag-fast` fast-only 探针：每 16ms 推 `--drag-fast-step` 帧，输出 fps/首帧/最大空档 |
| `src/app/main.cpp` | `--drag-fast` / `--drag-fast-step` 参数；测试模式/自动打开排除 |
| `docs/perf/local-drag-fast-2026-10-24.md` | 证据 |

**未改**：`OnlinePreviewDecoder`、T8.6.1b 语义、在线缓存、M1/M2 门/数字/OB/schema。

## 3. 结果

| 项 | 修复前 | 修复后 |
|---|---|---|
| 快拖 +60 帧/16ms | 28.6fps / 93.8ms | **61.0fps / 31.7ms** |
| 快拖 +120 | — | 59.2fps / 32.4ms |
| 快拖 +300 | — | 59.9fps / 32.3ms |
| 反向快拖 -60 | 约 23fps / >700ms | **42.2fps / 48.5ms** |
| 反向小步 -20 | — | **11.5fps / 191.8ms（残余）** |
| `--drag-test` P4 | 55.4fps | **61.3fps pass** |
| P6 松手精确 | — | **16.6ms** |
| P7 / P8 / P9 | — | 0.0/9.4ms、12.7fps、31.1ms 全 pass |
| `lapian_preview_decoder_test` | pass | pass（2s 拖动 63.85fps、反向 40 步精确） |
| `ctest` | 36/36 | **36/36**（63.85s） |
| key-test | 77/0 | **77/0** |
| T8.6.1b 在线预览 | 35/0 | **35/0（回归引用，未改 OnlinePreviewDecoder）** |

## 4. 残余/边界

1. **反向小步 -20 帧/16ms 只有 11.5fps**：软件解码每个目标都要 seek 关键帧，
   本轮只能做到“不冻住 + 快反向 42fps”，不能完全任意反向 60fps。
2. **4K/HEVC/10-bit 未换硬解预览**：不写 4K 快拖通过；若要 AE/电影电视级
   4K 任意拖动，需 P1 硬件预览（共享 D3D11 设备；另行任务）。
3. 正向快拖显示的中间帧可能落后目标帧；松手仍精确；这是预览语义。
4. 预览缓存 32 帧/约 44MB，只在拖动期间存在；未改正式 FrameCache。
5. 未改 P4/P7 验收值；本轮只提升体验，不把“60fps”写成全片保证。
6. 未重跑 M1/M2 门（引用 T8.8/T8.14）；未改 `OnlineSession`。

## 5. 命令

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'
& $exe --file media\测试.mp4 --drag-fast --drag-fast-step 60
& $exe --file media\测试.mp4 --drag-fast --drag-fast-step -60
& $exe --file media\测试.mp4 --drag-test --drag-samples 20
ctest --test-dir build\release -C Release --output-on-failure
```

## 6. 下一步

- 若用户仍觉得反向/4K 不够：立 **P1 硬件预览**（共享 D3D11 设备、硬件帧直接
  上传、在线 `OnlinePreviewDecoder` 同步评估），不要继续堆软件 seek 参数。
- 建议把 `--drag-fast` 纳入发布前体验回归（不是 M1/M2 门），目标保持
  “1080p 正向 ≥30fps、最大空档 ≤50ms”；4K 只记录。
- 当前工作区提交以 `git log -1` 为准；不改 T8.15 的 partial 结论。
