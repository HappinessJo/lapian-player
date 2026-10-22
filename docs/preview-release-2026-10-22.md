# 拉片软件 0.1.0 内部预览版收口（2026-10-22）

> **这是内部预览/自用版，不是 T7.7 验收通过，也不是“发布就绪”。**
> 用户于 2026-10-22 决定：手头没有真实 V1–V10 与基线机，停止正式 T7.7/P14
> 验收，按“开发完成、资源阻塞、未验收”收口；保留验收清单，日后有资源再补。
>
> 验收口径仍以 [spec.md](../spec.md) v0.3 为准；本文只记录“当前版本能做什么、
> 哪些没证”。不要拿本文去写 T7.7/P1–P14/P14 通过。

## 1. 结论

| 项 | 状态 |
|---|---|
| 功能开发 F1–F8 | 完成（T1–T7.11.1 代码闭环） |
| 构建/测试 | `ctest` **22/22**；`dist` **25 文件 / 32.65MB** |
| 分发包 | setup **11.31MB**、便携 zip **13.66MB**（P12 ≤100MB） |
| 开发机预检 | 合成片单 9/9 + V8 5/5 `OK=0`；P7 2s 65.33ms/1s 30.07ms |
| 正式 T7.7 | **未验收，阻塞**（真实片单 0/10、非基线机、冷缓存/真双击未做） |
| 正式 P14 | **未验收**（真实 V10 2h 未跑） |
| 当前定位 | **内部预览/自用**，不对外宣称发布就绪 |

## 2. 交付物与校验

| 形态 | 文件 | 体积 | SHA-256 |
|---|---|---:|---|
| 程序目录 | `dist\` | 25 文件 / 32.65MB | exe：`67F18473483F18174A4DD39D86BA181A62E0B8766F958F703E21439FDE55A013` |
| 便携 zip | `out\package\Lapian-0.1.0-portable.zip` | 13.66MB | `9AD69C1D1B4BE7BA350E572A91E4C2E3C49119D50AFBB65C6F332365F7834272` |
| 每用户安装包 | `out\package\Lapian-0.1.0-setup.exe` | 11.31MB | `B6990F616F70004F41A0E2894541AC99F044264A704D4957DA35C66A4C10D187` |

- 包体由代码提交 `7af822e` 构建；之后的文档提交不改 exe。
- 详细包体记录：`out\package\package-report.txt`（HEAD `7af822e`）。
- 校验命令：

```powershell
Get-FileHash dist\lapian.exe -Algorithm SHA256
Get-FileHash out\package\Lapian-0.1.0-setup.exe -Algorithm SHA256
Get-FileHash out\package\Lapian-0.1.0-portable.zip -Algorithm SHA256
```

## 3. 已验证项

- `ctest --test-dir build\release -C Release` → **22/22**（15.26s）。
- `scripts\check-imports.ps1` → 20 个文件，无 QtNetwork/TLS/winhttp/wininet/urlmon。
- 合成片单 `media\synthetic-suite\`：V1–V7/V9/V10 存在；V8 五个坏文件全部
  `OK=0 ERROR=corrupt`（0.87–3.26ms）。
- 开发机 P7 跨 GOP 边界（20 样本，dist）：
  2s GOP p50 59.31 / p95 **65.33** / max 67.02ms，0/20 超 80，余量 14.67ms；
  1s GOP p50 21.32 / p95 **30.07** / max 31.58ms，`short_gop pass=1`。
- P12：dist/setup/zip 均 ≤100MB。
- 安装/关联/卸载：T7.6 在本机通过；**当前机器未安装关联**，内部预览直接用
  `dist\lapian.exe` 或便携 zip 即可。

## 4. 未验证 / 已知风险

| 项 | 风险 |
|---|---|
| 真实 V1–V10 | 功能手测、P6/P9 分类、真实 VFR/HEVC/4K/坏文件未正式验收 |
| 基线机 P1–P14 | 4C/8GB/集显/SATA SSD 上冷启动、拖动、步进、内存都可能比开发机慢 |
| 冷缓存 F1-A1 / Explorer 真双击 | 未做；热缓存启动数字不能代表冷启动 |
| P7 2s 余量 | 开发机 14.67ms；基线机 iGPU/4 核可能超 80ms，需另立优化任务 |
| 真实 V4 4K | 目前只有 proxy-V4；真实片源的 GOP/内容可能不同 |
| 真实 V5 VFR / V8 坏文件 | 只有人工 VFR/合成坏文件，不能替代真实素材 |
| 真实 V10 / P14 | 没有 2h 连续播放证据；iGPU 共享内存下 WS 可能更高 |
| 真实 100%–200% DPI | 只有 `QT_SCALE_FACTOR` 模拟布局 |
| 物理设备丢失 / 可达 SMB | T7.5 仍是模拟/不可达 UNC，不是物理/SMB 实物 |
| 安装器 | UI 英文、未签名、无图标；这些是已知缺口，不影响自用 |

> 这些风险里，最需要关注的是 **P1/P2 冷启动**、**基线机 P7 2s**、
> **真实 4K P6/P9**、**P14 2h 内存**。如果只是自用，可以先按下面的方式使用；
> 如果要对外发布，仍应补齐正式验收。

## 5. 怎么用（内部预览）

### 5.1 最简：便携版

1. 解压 `out\package\Lapian-0.1.0-portable.zip` 到本地目录（建议 C: 或 D:）。
2. 运行 `lapian.exe`。
3. 打开视频：`Ctrl+O`、拖拽，或运行 `lapian.exe --file "路径\视频.mp4"`。

便携版不需要管理员、不写注册表关联；设置/标记仍按产品逻辑写
`%APPDATA%\拉片软件\` 或视频旁边。

### 5.2 安装版

`Lapian-0.1.0-setup.exe` 是每用户安装（不需要管理员），默认装到
`%LOCALAPPDATA%\Programs\拉片软件`。**不要从 E: 盘直接运行安装包**：
本机 E: 被 Windows 标记为不受信任位置，可能弹出“无法安装”；先复制到
C: 本地目录再运行。

安装后会注册 ProgID/OpenWithProgids（不设默认关联）。卸载会清理程序和
关联值，保留 settings/marks/sidecar 与截图。

### 5.3 数据位置

- 设置：`%APPDATA%\拉片软件\settings.json`。
- 标记/备注：优先视频同目录 `<视频名>.lapian.json`；不可写回退
  `%APPDATA%\拉片软件\marks\<路径哈希>.json`。
- 截图：视频同目录 `snapshots\`；不可写回退用户图片目录。

## 6. 以后恢复正式验收

资源到位后直接按 [T7.7 验收清单](perf/T7.7-acceptance-checklist.md) 执行；
预检用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\t7.7-preflight.ps1 -RunCTest
```

- 真实素材放 `media\acceptance\V1.mp4 ... V10.mp4`、`media\acceptance\V8\`；
- 基线机 4C/8GB/集显/SATA SSD/Win11；
- 冷缓存需要重启 + RAMMap + Explorer 真双击；
- P14 用真实 V10 跑 7200s（`-NoLoop`）。

如果基线机 P7 2s 超 80ms，不要改阈值；按 T7.11.1 证据评估“seek 到关键帧后
的空闲预解”或 D3D11 批量回拷，作为新任务。

## 7. 不要宣称

- 不要写“T7.7 通过”“P1–P14 通过”“P14 通过”“发布就绪”。
- 不要把合成片单写成正式 V1–V10；不要把 proxy-V4 写成真实 V4。
- 不要把热缓存 `startup-bench` 写成冷缓存 F1-A1。
- 不要把开发机 P7 65.33ms 当成基线机通过；2s 余量只有 14.67ms。
- 不要把这个预览版当成对外发布的最终包；安装器英文/未签名/无图标仍是缺口。

## 8. 决策记录

- 2026-10-22：用户确认手头没有真实 V1–V10/基线机，选择“自己用/内部预览”，
  停止正式 T7.7/P14 验收；版本按内部预览收口，T7.7/T7.3 保持 blocked，
  不标完成、不宣称通过。验收清单与预检脚本保留，资源到位后可恢复。
