# 下一个会话提示词：T8.9（M2 DASH 解析与档位）

> 直接复制本文件给新会话。当前会话已完成 T8.6.1a（含 hotfix 与用户实机确认）；
> **不要在本会话继续 T8.9**。新会话只做 **T8.9**，不要顺带做 T8.10/T8.11/T8.6.1b。

## 1. 先读

1. [AGENTS.md](AGENTS.md)（当前项目状态/约束）
2. [tasks-v2.md](tasks-v2.md) **T8.9** 行与版本记录（当前 v0.18）
3. [docs/spec-v1.0-delta.md](docs/spec-v1.0-delta.md) v0.4（OP/DEC/O11）
4. [docs/perf/v2-online-spike.md](docs/perf/v2-online-spike.md)（S2 档位矩阵）
5. [T8.6.1a 交接](docs/handoff-2026-10-24-t8.6.1a.md)、[T8.8 M1 交接](docs/handoff-2026-10-23-t8.8-m1.md)
6. [ADR-007](docs/adr/ADR-007-bilibili-online.md)（附条件接受；D1–D5 不改）

## 2. 任务边界

**只做 T8.9：DASH 解析与档位。**

- `playurl(fnval=4048)`；解析 `dash.video[]` / `dash.audio[]`。
- 字段：`qn`、`codecid`、`fps`、带宽、size、`init`/`index` Range、备份 URL。
- 默认 AVC；档位顺序；4K 只手动；档位菜单；`quality` 字段不可信，以实际返回为准。
- 错误 qn 不崩；音轨存在。
- **不做**：T8.10 双输入、T8.11 渐进索引/波形、T8.6.1b 拖动预览、T8.12 策略、T8.13 加固、T8.14 M2 门、T8.15 VIP 条件验收、T8.16 打包。
- **不改** M1/M2 门、验收数字、OB 样本；不把 1080P60/4K 写成保证；不把大会员档写成 MVP。

## 3. 验收（tasks-v2 T8.9）

- 3 条档位矩阵与 S2 一致（登录普通用户 1080P30 3/3）。
- 普通用户 60/4K 不可得记录在案；1080P60/4K 进菜单（只手动）。
- 大会员档按 O11/T8.15 条件验收，不写成 MVP。
- HEVC-only 60fps 只手动；错误 qn 不崩；音轨存在。
- 不把 durl 1080p 1/3 当保证。
- 证据：新增/更新 `docs/perf/v2-online-quality.md`；产品探针 `--online-quality-test`；交接 `docs/handoff-*-t8.9.md`。

## 4. 代码入口（先读再改）

- `src/online/PlayUrl.*`：现有 durl/fnval=0 取流；确认 `fnval=4048` 请求与响应结构。
- `src/online/OnlineSession.*`：在线会话/缓存/首帧；T8.9 只接解析结果，不接双输入。
- `src/app/MainWindow.*`：在线入口/清晰度菜单（如有）；不要改本地模式。
- `src/online/DownloadJob.*`：durl 下载路径；T8.9 不重写下载，只准备 DASH 描述/备份 URL。
- `src/app/main.cpp`：现有 `--online-*` 探针参数；新增 `--online-quality-test` 时复用凭据/bvid/cid/timeout。

先查当前 `fnval`、durl 请求和 M1 `kM1RequestQn=80` 的行为，不要假设 DASH 响应一定含全部字段。

## 5. 工作区与测试约束

- **构建只在工作区**：`scripts\configure.ps1` + `build\release`；产物 `build\release\bin\lapian.exe`。
- 在线探针用显式测试根，不写 AppData：

```powershell
.\build\release\bin\lapian.exe --online-quality-test `
  --bvid ... --cid ... --tag t8.9-... --timeout-ms 30000 `
  --online-cache-root build\t8.9-...\cache `
  --online-pictures build\t8.9-...\pics `
  --online-records-root build\t8.9-...\records `
  --longpress-fresh-cache
```

- 工作区 exe 是 Low integrity；默认 UI 在线缓存会写 Medium AppData 失败。
  **不要**再把 exe 复制到 `%LOCALAPPDATA%\Programs\拉片软件` 做开发验证；需要真实 UI
  手动验收时再说明。探针显式根已足够。
- 凭据只从 `.secrets`/产品 UI 读取；日志不打印 SESSDATA/签名 URL。

## 6. 停止条件

- 要改 M1/M2 门、验收数字、OB 样本：先停下问用户。
- 发现必须做 T8.10 双输入才能继续：先记录 blocker，不要自行扩任务。
- 大会员/60/4K 账号不可得：写“未验证/不可得”，不要写成通过。
- 不要进 T8.9 以外的任务；不要用 durl 1080p 的 1/3 通过冒充 M2。

## 7. 完成时

- 一个可审查 commit；`git status` clean。
- 更新 `docs/perf/v2-online-quality.md`、T8.9 交接、`tasks-v2.md`（v0.19）与 `AGENTS.md`。
- 报告：档位矩阵数字、实际 qn/codecid/fps、失败/不可得项、未验证项、下一步 T8.10。
