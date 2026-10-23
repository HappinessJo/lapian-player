# 下一个会话提示词：T8.9（M2 DASH 解析与档位）

- **版本**：v2（2026-10-24，T8.3.1 交接后刷新；基线 commit `cbab29b`）
- **直接复制本文件给新会话。** 当前会话已完成 T8.3.1（缓存管理，非门）；**不要在本会话继续 T8.9**。新会话只做 **T8.9**，不要顺带 T8.10/T8.11/T8.6.1b。
- **当前状态**：M1 已于 T8.8 通过；M2 未开始；`tasks-v2.md` v0.19；`AGENTS.md` v6.29；`ctest` 31/31；dist 33.87MB/25。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（当前状态/约束；v6.29）
2. [tasks-v2.md](../tasks-v2.md) **T8.9** 行、M2 门、版本记录（v0.19；门与数字锁定）
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) v0.4（O11/OP1–OP10/DEC-2/DEC-4/DEC-5）
4. [v2-online-spike.md](perf/v2-online-spike.md)（S2 档位矩阵/匿名与普通用户）
5. [T8.8 M1 交接](handoff-2026-10-23-t8.8-m1.md)、[T8.6.1a 交接](handoff-2026-10-24-t8.6.1a.md)、**[T8.3.1 交接](handoff-2026-10-24-t8.3.1.md)**
6. [ADR-007](adr/ADR-007-bilibili-online.md)（附条件接受；D1–D5 不改）
7. 代码：`src/online/PlayUrl.*`、`BiliResolver.*`、`WinHttpClient.*`、`src/app/OnlineSession.*`、`MainWindow.*`、`src/online/CacheStore.*`/`CacheManager.*`

## 2. 任务边界

**只做 T8.9：DASH 解析与档位。**

- `playurl(fnval=4048)`；解析 `dash.video[]` / `dash.audio[]`。
- 字段：`qn`、`codecid`、`fps`、带宽、size、`init`/`index` Range、备份 URL。
- 默认 AVC；档位顺序；4K 只手动；档位菜单；`quality` 字段不可信，以实际返回为准。
- 错误 qn 不崩；音轨存在。
- **不做**：T8.10 双输入、T8.11 渐进索引/波形、T8.6.1b 拖动预览、T8.12 策略/降帧提示、T8.13 加固、T8.14 M2 门、T8.15 VIP 条件验收、T8.16 打包。
- **不改** M1/M2 门、验收数字、OB 样本；不把 1080P60/4K 写成保证；不把大会员档写成 MVP。
- T8.3.1 的缓存管理语义必须保留：清空/孤儿清理走 `CacheManager`，当前会话用 `bvid+cid` 部分键保护；T8.9 不改 `lapian-cache-1` schema、不改 DASH 音频缓存键（留给 T8.10）。

## 3. 验收（tasks-v2 T8.9）

- 3 条档位矩阵与 S2 一致（登录普通用户 1080P30 **3/3**）。
- 普通用户 60/4K 不可得记录在案；1080P60/4K 进菜单（只手动）。
- 大会员档按 O11/T8.15 条件验收，不写成 MVP。
- HEVC-only 60fps 只手动；错误 qn 不崩；音轨存在。
- 不把 durl 1080p 1/3 当保证；不把“菜单可选”写成“保证可达”。
- 证据：新增/更新 `docs/perf/v2-online-quality.md`；产品探针 `--online-quality-test`；交接 `docs/handoff-*-t8.9.md`。

## 4. 代码入口（先读再改）

- `src/online/PlayUrl.*`：现有 durl/fnval=0 取流；确认 `fnval=4048` 请求与响应结构。
- `src/online/BiliResolver.*` / `WinHttpClient.*`：登录态、`view`/`playurl` 请求；不要绕过现有 Cookie/超时/取消。
- `src/app/OnlineSession.*`：在线会话/缓存/首帧；T8.9 只接解析结果，不接双输入。
- `src/app/MainWindow.*`：在线入口/清晰度菜单（如有）；不要改本地模式。
- `src/online/DownloadJob.*`：durl 下载路径；T8.9 不重写下载，只准备 DASH 描述/备份 URL。
- `src/online/CacheStore.*` / `CacheManager.*`：不要绕过其清空/孤儿/配额语义；T8.9 不新增下载键。
- `src/app/main.cpp`：现有 `--online-*` 探针参数；新增 `--online-quality-test` 时复用凭据/bvid/cid/timeout。

先查当前 `fnval`、durl 请求和 M1 `kM1RequestQn=80` 的行为，不要假设 DASH 响应一定含全部字段；`quality` 可能撒谎。

## 5. 工作区与测试约束

- **构建只在工作区**：`scripts\configure.ps1` + `build\release`；产物 `build\release\bin\lapian.exe`。
- 在线探针用显式测试根，不写真实 AppData：

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

build\release\bin\lapian.exe --online-quality-test `
  --bvid ... --cid ... --tag t8.9-... --timeout-ms 30000 `
  --online-cache-root build\t8.9-...\cache `
  --online-pictures build\t8.9-...\pics `
  --online-records-root build\t8.9-...\records
```

- 基线回归（不得退化）：`ctest` 31/31；`--online-cache-manage-test` workspace 72/0、`--ui` 98/0；本地 `--key-test` 77/0；dist 33.87MB/25。
- **默认路径/真实 UI 只在 Medium 副本验证**：`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`。需要时先备份真实 `cache`/`online`/`settings.json` 与原 exe，结束恢复；**不要用工作区 exe 冒充默认路径**。T8.9 探针本身用显式根即可。
- 工作区 exe 是 Low integrity；默认 UI 在线缓存写 Medium AppData 会失败，Low 结果不代表正常用户。
- 凭据只从 `.secrets`/产品 UI 读取；日志不打印 SESSDATA/签名 URL；不提交 `.secrets`、缓存、截图、记录。

## 6. 停止条件

- 要改 M1/M2 门、验收数字、OB 样本：先停下问用户。
- 发现必须做 T8.10 双输入才能继续：先记录 blocker，不要自行扩任务。
- DASH/playurl 接口变动、WBI/风控要求新增认证或绕过：停下记录，不要猜。
- 大会员/60/4K 账号不可得：写“未验证/不可得”，不要写成通过。
- 不要进 T8.9 以外的任务；不要用 durl 1080p 的 1/3 或菜单存在冒充 M2。

## 7. 完成时

- 一个可审查 commit；`git status` clean。
- 更新 `docs/perf/v2-online-quality.md`、T8.9 交接、`tasks-v2.md`（v0.20）与 `AGENTS.md`（v6.30）。
- 报告：3 条档位矩阵、实际 qn/codecid/fps/宽高、可用/不可得项、失败 qn、音轨字段、durl 对照、未验证项、下一步 T8.10。
