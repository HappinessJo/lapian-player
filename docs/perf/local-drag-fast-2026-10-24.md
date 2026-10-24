# 本地拖动预览快速大跳热修（2026-10-24）

- **版本**：v1.0
- **状态**：**用户反馈热修，非门、非 MVP 验收**。对象是本地播放的拖动预览
  （`PreviewDecoder`）；不改变 P4/P7 验收数字、M1/M2 门、在线缓存 schema。
- **基线**：T8.15 提交 `1014b20`；`ctest` 36/36、key 77/0、dist 34.48MB/25。
- **用户反馈**：快速拖动时间轴时画面卡住不渲染；即使本地视频也如此，
  对比 AE（缓存好时）与 Windows 电影和电视的跟手拖动。

---

## 1. 复现样本与基线

`media\测试.mp4`：

| 项 | 值 |
|---|---|
| 尺寸/编码 | 1920×1080 / H.264 / yuv420p |
| fps / 帧数 | 30.000 / 4573 |
| GOP | **30 帧（1s）**，153 个关键帧 |
| 软件精确 seek | p50 **26.5ms**、p95 **77.9ms** |
| 硬件 seek（同文件） | p50 **12.8ms**、p95 **29.4ms** |

修改前产品路径（`--drag-test` / 临时大跳探针）：

| 场景 | 修改前 |
|---|---|
| 小步拖动 P4（每 16ms +3 帧） | 55.4fps，P4 pass |
| **快速大跳**（每 16ms +60 帧，约 2s/次） | **28.6fps，最大呈现空档 93.8ms** |
| 反向快速大跳 | 很低，且经常整段无帧 |

根因：`PreviewDecoder` 是独立**软件**预览解码器；每个鼠标事件都会让
`generation+1`，解码线程下一帧就发现“目标过期”并放弃；大跳要重新 seek
关键帧，快速拖动时反复“seek→解一帧→取消”，画面就空住。反向拖动又只发布
“正好等于目标”的帧，中间不显示任何东西。现有 P4 只测 +3 帧小步，覆盖不到。

## 2. 本轮修改

### 2.1 目标版本与拖动会话分离

`src/media/PreviewDecoder.cpp`：

- `generation` 继续表示“目标版本”，新增 `session` 表示“本次拖动”；
  `Publish/CacheFrame/CloneCached` 只按 session 失效，**不再因为目标变化丢弃
  已解码帧**。
- `Process` 每解一帧读取**最新目标**：
  - 正向：目标前进时继续追最新目标，发布中间帧，不中断重 seek；
  - 反向：先解完当前目标再取最新目标，避免每个 16ms 事件都中途放弃；
  - 小步反向命中 32 帧预览缓存；快速反向允许“比当前目标晚、但比上一张已显示
    帧更早”的粗帧，保证画面持续后退；松手仍走精确 `GoToFrame`。
- 正向连续解码窗口从 2s 放宽到 **5s**；刚 seek 过的远跳会继续解完，避免
  “解到第一帧→发现还远→再 seek”的死循环。

### 2.2 新探针 `--drag-fast`

`DragTestRunner` 新增 fast-only 模式：每 16ms 把目标向前/向后推
`--drag-fast-step` 帧（默认 60 帧≈2s），统计真正 Present 的 fps、
首帧延迟、相邻 Present 最大空档。用于本次修复和后续回归：

```powershell
build\release\bin\lapian.exe --file media\测试.mp4 --drag-fast --drag-fast-step 60
build\release\bin\lapian.exe --file media\测试.mp4 --drag-fast --drag-fast-step -60
```

## 3. 修复后结果

同一台机器、同一 `media\测试.mp4`（硬解开启）：

| 场景 | 修复前 | 修复后 |
|---|---|---|
| 快速大跳 +60 帧/16ms | 28.6fps / 空档 93.8ms | **61.0fps / 空档 31.7ms** |
| 快速大跳 +120 帧/16ms | 未测 | **59.2fps / 32.4ms** |
| 快速大跳 +300 帧/16ms | 未测 | **59.9fps / 32.3ms** |
| 反向快速大跳 −60 帧/16ms | 约 23fps / 空档 >700ms | **42.2fps / 48.5ms** |
| 反向小步 −20 帧/16ms | — | **11.5fps / 191.8ms（残余）** |

现有回归：

| 项 | 结果 |
|---|---|
| `--drag-test` P4（+3 帧/16ms） | **61.3fps**（修改前 55.4），P4 pass |
| P6 松手精确 | p95 **16.6ms**（≤150ms） |
| P7 back/fwd | p95 **0.0 / 9.4ms**，pass |
| P8 长按 | 12.7fps / 26 帧，pass |
| P9 跳转 | p95 **31.1ms**，pass |
| `lapian_preview_decoder_test` | 全部通过；2s 拖动 **63.85fps**；反向 40 步精确/单调 |
| `ctest` | **36/36**（63.85s） |
| 本地 key-test | **77/0** |
| T8.6.1b 在线预览 | **35/0 pass**（未改 `OnlinePreviewDecoder`；MainWindow 仅删调试日志） |

## 4. 口径与残余

- **目标仍按现行 P4**：拖动预览 ≥30fps、松手 ≤150ms；本次是体验热修，
  **不改 P4/P7 数字**，也不写“60fps 全片保证”。
- 正向快拖现在会显示“追赶中的中间帧”，画面帧号可能落后 InfoBar 目标帧；
  松手仍精确到目标帧。这是拖动预览，不是最终落点。
- **反向小步 −20 帧/16ms 仍是 11.5fps**：每个目标都要 seek 到关键帧，软件
  解码无法只靠算法完全消除；该场景属于“极端反向小步”，保留为残余。
- **4K/HEVC/10-bit**：本轮未换硬解预览，仍可能受软件解码限制；不写 4K 快拖
  通过。若需要 AE/电影电视级任意位置 4K 拖动，需要硬件预览（P1，另行任务）。
- 预览缓存最多 32 帧（约 44MB @720p NV12），只在拖动期间存在；未改正式
  `FrameCache` 预算。
- 未改 M1/M2 门/数字、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p；
  未据此写 MVP 通过。

## 5. 复现

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'
& $exe --file media\测试.mp4 --drag-fast --drag-fast-step 60
& $exe --file media\测试.mp4 --drag-fast --drag-fast-step 120
& $exe --file media\测试.mp4 --drag-fast --drag-fast-step -60
& $exe --file media\测试.mp4 --drag-test --drag-samples 20
```

> 注：`--drag-fast` 使用产品路径的 `ScrubStart/Target/Finish` 与
> `framePresented` 信号；`fps` 是实际 Present 帧数/秒，最大空档不含首帧延迟。
