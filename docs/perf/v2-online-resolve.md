# T8.1 在线解析层测量与验收记录（v2）

- **版本**：v1.0（2026-10-23）
- **任务**：T8.1 解析层：BV/URL 归一化 → 本地校验 → 匿名 `view` 元数据 → 回显确认。
- **结论**：**T8.1 范围内通过**。本记录不代表 T8.2（登录自检/HTTP 栈）、T8.5（取流）、M1/M2 通过；不含播放、缓存、DASH 档位。
- **代码状态**：本次工作区变更；基线 `b6b2239`（v2 文档拆分）。未提交 `.secrets`。

> 不写进本记录：4K / 1080P60 / 720P60 验收；未缓存区 80ms；durl 1080p 保证。T8.1 只解析元数据，未取流、未选档。

---

## 1. 环境与命令

| 项 | 值 |
|---|---|
| 日期/时区 | 2026-10-23 01:2x–01:3x（Asia/Shanghai） |
| OS | Windows 11 10.0.26200（CMake 选择 SDK 10.0.26100.0） |
| 编译器/Qt/FFmpeg | MSVC 19.44 / Qt 6.8.3 / FFmpeg 7.1.1 minimal |
| 机器 | 开发机；NVIDIA RTX 4060（仅记录环境，T8.1 不测渲染） |
| 网络 | 单机、单时段、匿名；`api.bilibili.com` HTTPS RTT 65–230ms；未登录、无 Cookie |
| 构建 | `build\release`，Release |

命令（在仓库根目录、Qt `bin` 加入 `PATH` 后）：

```powershell
# 构建
cmake --build build\release --config Release

# 全量单测
ctest --test-dir build\release -C Release --output-on-failure

# T8.1 单测（含本机慢服务器测 WinHTTP 取消/超时）
build\release\Release\lapian_online_resolve_test.exe

# 产品对话框集成探针（OB-1/2/3、OB-5、多P、串片、取消/关窗）
build\release\bin\lapian.exe --online-resolve-test

# 打包产物上的同一探针
dist\lapian.exe --online-resolve-test

# 本地模式回归
build\release\bin\lapian.exe --startup-test --file media\proxy\proxy-V1.mp4
build\release\bin\lapian.exe --gop-boundary-test --file build\test-media\real-gop2s-full.mp4 --samples 20
build\release\bin\lapian.exe --key-test --file build\test-media\real-gop2s-full.mp4

# dist + 导入表检查
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -SkipTests
```

本地证据文件（构建产物，未入库）：`build\t8.1-ctest-final.log`、`build\t8.1-online-resolve-unit.log`、
`build\t8.1-online-resolve-test.log`、`build\t8.1-online-resolve-test-dist.log`、
`build\t8.1-local-startup.log`、`build\t8.1-local-gop2s.log`、`build\t8.1-local-key2.log`、
`build\t8.1-dist.log`。

---

## 2. 单测结果

`ctest`：**23/23 通过，0 失败，17.46s**（含既有 22 项；新增 `lapian_online_resolve_test` 1.76s 量级）。

`lapian_online_resolve_test`：**81 项检查 / 0 失败**。覆盖：

- BV：裸 BV、小写 `bv` 前缀、11/13 位、非法字符、空串/空白；
- URL：标准、`?p=2`、尾斜杠+额外参数、无 scheme、协议相对、http→请求 https；
- 拒绝：`av` 号、`b23.tv` 短链、非 B 站 host、非视频页、`p=0`/`p=abc`、搜索词；
- view JSON：标题/UP/时长/分P/大 cid（`42915922022` > 2³¹）、6 分P、`SelectPage(2/3)`、越界；
- 错误码：`-400`/`-404`/`62002`/`-101`/未知码通用提示；坏 JSON/缺 code/缺 data/空分P；
- `BiliResolveClient`：非法输入 **0 请求**；URL 只含 bvid；HTTP 429/500/超时/API -404 映射；请求前/请求中取消；
- `WinHttpClient`：本机慢服务器 200、超时、取消、连接拒绝；请求头无 `Cookie`/`SESSDATA`；首次解析后才加载 `winhttp.dll`。

关键数字（本机单次）：

| 项 | 实测 |
|---|---|
| `BiliResolveClient` 请求中取消 | **15ms**（≤100ms） |
| WinHTTP 请求中取消（本机慢服务器） | **15ms**（≤100ms） |
| WinHTTP 400ms 超时 | **427ms** 返回 `timed_out` |
| 首次解析前 `winhttp.dll` | 未加载；首次 `Get` 后加载 |
| 本机慢服务器请求头 | 无 `Cookie:`、无 `SESSDATA` |

---

## 3. 产品对话框集成（`--online-resolve-test`）

`build\release\bin\lapian.exe --online-resolve-test`：**156 项检查 / 0 失败**；`summary checks=156 failures=0 max_local_invalid_ms=0 confirmed_events=2`。
`dist\lapian.exe --online-resolve-test`：同样 **156/0**（打包产物）。

### 3.1 成功样本（匿名 view）

| 样本 | 输入 | 回显标题 | UP 主 | 总时长/分P时长 | 分P | cid | 请求 | 解析耗时 |
|---|---|---|---|---|---|---|---|---|
| OB-1 | `BV1BMnv6jE5S` | 在超市后门吸烟的二人【静止系MAD】 | 忆雪猫 | 01:51 / 01:51 | P1 | 42915922022 | 1 | 230ms |
| OB-2 | `BV1mA411m7pS` | 【静止画MAD】-よふかしのうた by fvrein [UHD 60FPS][CC字幕] | benzun_ | 01:36 / 01:36 | P1 | 1004967230 | 1 | 94ms |
| OB-3 | `BV1GJ411x7h7` | 【官方 MV】Never Gonna Give You Up - Rick Astley | 索尼音乐中国 | 03:33 / 03:33 | P1 | 137649199 | 1 | 85ms |
| URL 形式 | `https://www.bilibili.com/video/BV1BMnv6jE5S?p=1&spm_id_from=333.999` | 同 OB-1 | 同 OB-1 | 01:51 | P1 | 42915922022 | 1 | 77ms |

- 三条 OB 都有标题/UP/时长/分P；**没有**发起 playurl、没有播放（`HasMediaForTest=false`）。
- 未登录提示为“**未登录：清晰度受限（实际档位以取流时返回为准）**”，未承诺 480P/720P/1080P。
- 只请求 `https://api.bilibili.com/x/web-interface/view?bvid=...`；无 Cookie、无签名参数。

### 3.2 多P与切P（额外样本，不属于 OB 最低集）

`https://www.bilibili.com/video/BV1fNnv6EEon?p=2`，view 返回 6 个分P；默认选 P2：

| 操作 | 分P | 时长 | cid | 结果 |
|---|---|---|---|---|
| 输入 `?p=2` 回显 | P2 | 00:57 | 42915138952 | 总时长 08:21；6 个分P |
| 切到 P1 | P1 | 01:50 | 42915136857 | 已确认状态被重置，需重新确认 |
| 切到 P3 | P3 | 01:50 | 42915201367 | 确认事件携带 P3/cid |
| 点“确认” | P3 | — | — | 只发确认信号；未取流、未播放 |

该项证明“默认 P1 / 可切换 / 切P后 cid 与时长正确”。`p=2` 的纯单测还覆盖了 JSON 分P 选择（不依赖网络）。

### 3.3 串片样本（O1-A4）

| 输入 | 实际回显 | 实际时长 | 确认 |
|---|---|---|---|
| `BV1mA411m7pX`（OB-1 末位改动） | 精灵宝可梦第四季114：N再度登场 救出勇士鹰 | 02:52 | 回显后“确认”才可点；确认前 `Confirmed=false`、无播放 |

与 OB-1 标题不同（测试断言 `mismatch-title-differs`）。**没有只凭输入就播放**；T8.1 没有播放路径。

### 3.4 失败矩阵

| 情况 | 输入 | 结果 | 请求数 | 用户提示 |
|---|---|---|---|---|
| 格式非法（`BV1`） | `BV1` | 本地拒绝，**不发请求** | 0 | “BV 号格式不对：应为 BV 开头的 12 位字母数字（当前 3 位）” |
| 少一位 | `BV1BMnv6jE5` | 本地拒绝，**不发请求** | 0 | 同上（11 位） |
| 含非法字符 | `BV1BMnv6jE5!` | 本地拒绝，**不发请求** | 0 | “BV 号含非法字符：只允许字母和数字” |
| 空串/空白 | `""` / `"   "` | 本地拒绝，**不发请求** | 0 | “请输入 BV 号或 B 站视频链接” |
| 参数错误 | `BV0000000000`（本地合法 12 位） | API `-400` | 1 | “请求参数错误（-400）：请检查 BV 号或链接后重试” |
| 不存在 | `BV1zz411c7mD` | API `-404` | 1 | “稿件不存在或已删除（-404）” |
| 不可见 | `BV1Q541167Qh` | API `62002` | 1 | “稿件不可见（私密/审核/删除，62002）” |
| 串片 | `BV1mA411m7pX` | API `0`，另一条视频 | 1 | 回显实际标题/时长并要求确认 |

说明：OB-5 原始的 `BV1` 与“少一位”按 O1-A2 属**本地非法**，不发请求；因此 live `-400` 用本地合法的
`BV0000000000`（API 返回 `-400 请求错误`）覆盖。两者都在测试里，未把本地非法伪装成远端 `-400`。

### 3.5 本地非法输入的 0 网络与 ≤100ms

| 输入 | 耗时 | 本次请求数 | 累计请求数 | 错误 |
|---|---|---|---|---|
| 空串 | 0ms | 0 | 0 | 请输入… |
| `BV1` | 0ms | 0 | 0 | 12 位格式 |
| `BV1BMnv6jE5` | 0ms | 0 | 0 | 12 位格式 |
| `BV1BMnv6jE5!` | 0ms | 0 | 0 | 非法字符 |
| `av170001` | 0ms | 0 | 0 | 不支持 av 号 |
| `b23.tv/abc` | 0ms | 0 | 0 | 不支持短链 |
| `https://example.com/video/BV1BMnv6jE5S` | 0ms | 0 | 0 | 只支持 bilibili.com |
| `...?p=0` | 0ms | 0 | 0 | p 必须是正整数 |
| `随便搜索一个视频` | 0ms | 0 | 0 | 不支持搜索词 |

`max_local_invalid_ms=0`（≤100ms）；`LastHttpRequests=0`、`TotalHttpRequests=0`，且日志中**没有**
`T8.1 http_request` 行。本地校验通过 `NormalizeInput` 同步完成，不创建 HTTP 客户端、不启动线程。

---

## 4. 取消、关窗与超时

| 场景 | 实测 | 证据 |
|---|---|---|
| 解析中点取消（fake 慢客户端，UI 路径） | `CancelForTest` **≤5ms**（多次运行 1–5ms）；worker 已 join，`HasWorkerThread=false` | `cancel_fake cancel_ms=5`（另次 1ms） |
| 解析中关窗（fake 慢客户端，UI 路径） | `close()` **≤7ms**（多次运行 3–7ms）；`LastCancelMs≤5ms`；worker 已 join | `close_fake close_ms=7 cancel_ms=5`（另次 3/0ms） |
| 真实 WinHTTP 请求中取消（本机慢服务器） | **15ms**；`cancelled=true`；`HANDLE_CLOSING` 已到达 | 单测 `winhttp_cancel_ms=15` |
| `BiliResolveClient` 请求中取消 | **15ms** | 单测 `resolve_client_cancel_ms=15` |
| 400ms 超时 | **427ms** 返回 `timed_out` | 单测 `winhttp_timeout_elapsed_ms=427` |
| 测试后残留进程 | 无 `lapian.exe` / `lapian_online_resolve_test.exe` | `Get-Process` |

说明：UI 取消/关窗测的是“产品对话框 + 工作线程 + 取消令牌”路径；真实 WinHTTP 请求中取消在本机慢服务器上测（同一 `WinHttpClient`）。
没有对 `api.bilibili.com` 做“人为拖慢再取消”的注入（view 正常 65–230ms），因此不把“真实 B 站请求中取消”写成已单独测过。

---

## 5. 网络请求记录与脱敏

- 只在用户提交合法 BV/URL 后请求：`GET https://api.bilibili.com/x/web-interface/view?bvid=<bvid>`；无 Cookie、无签名参数、无 referer。
- 日志行：`T8.1 http_request host=api.bilibili.com path=/x/web-interface/view timeout_ms=6000`；`path` **不含查询串**，`host` 固定。
- 结果行：`T8.1 http_done host=... status=200 elapsed_ms=... cancelled=0 timed_out=0`；不打印完整 URL/查询串/Cookie。
- 本地非法输入没有 `http_request` 行；`LastHttpRequests=0`。
- `WinHttpClient` 在请求上设置 `WINHTTP_DISABLE_COOKIES`；本机慢服务器抓到的请求头**无 `Cookie:` / `SESSDATA`**。
- `winhttp.dll` 动态加载（`LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`）；首次解析前未加载，dist 导入表无 `WINHTTP.DLL`。

未做：把请求交给代理抓包保存 pcap；本记录用“代码计数 + 请求日志 + 本机服务器请求头 + dist 导入表”四项作为证据。
T8.2 接入 SESSDATA 后必须再做一次“Cookie 只发 api.bilibili.com、不发 CDN”的抓包证据。

---

## 6. 本地模式回归（O9 范围内）

| 项 | 实测 | 说明 |
|---|---|---|
| P1 窗口可交互 | **277.78ms** | `--startup-test --file media\proxy\proxy-V1.mp4`；`window_shown` |
| P2 双击到首帧 | **334.30ms** | 同一命令；`first_present`；热缓存开发机 |
| P7 2s GOP 单步（后退跨 GOP） | **p95 67.37ms / p50 60.74ms / max 68.04ms，n=20，over=0** | `--gop-boundary-test --file build\test-media\real-gop2s-full.mp4 --samples 20`；`threshold_ms=80` |
| 键位/焦点矩阵 | exit 0；`key_shortcuts` 含 `online=Ctrl+U`；无 `ok=0` | `--key-test --file build\test-media\real-gop2s-full.mp4` |
| 本地运行网络日志 | 上述三次本地运行中 `T8.1 http` / `T8.1 resolve` 行数 = **0** | `LAPIAN_ONLINE_LOG=1` 下抓 stderr |
| dist 本地启动 | P1 267.18ms / P2 324.75ms | `dist\lapian.exe --startup-test ...` |

补充：`proxy-V1.mp4` 是 10s GOP（`gop_ms≈10416.7`，long_gop），其 PgUp 后退 p95 279ms 属“长 GOP 可用档”，
不是 P7 的 2s GOP 口径；本记录只把 `real-gop2s-full.mp4` 的 67.37ms 当作 P7 回归，不改 v1 口径。

---

## 7. dist 与导入表

`scripts\build-release.ps1 -SkipTests` 通过：

- `check-imports.ps1`：**20 个文件，无 QtNetwork/TLS/winhttp/wininet/urlmon**；仅记录已知的 `Qt6Core.dll -> WS2_32`。
- `dist`：**25 个文件 / 32.81MB**（P12 ≤100MB，余量 67.19MB）；与 T7.6 的 32.65MB 相比 +0.16MB（解析层代码）。
- `dist\lapian.exe --online-resolve-test`：156/0；`dist\lapian.exe --startup-test` 本地路径无 HTTP 日志。
- 没有把 `winhttp.dll` 拷进 dist；没有新增 Qt 网络 DLL/插件。

---

## 8. 截图

| 文件 | 内容 |
|---|---|
| `docs/perf/T8.1-online-resolve-ob1.png` | OB-1 回显：标题/UP/总时长/分P/分P时长/cid/未登录提示；确认可点 |
| `docs/perf/T8.1-online-resolve-multip.png` | 6 分P、`?p=2` 选中 P2、总时长 08:21、分P时长 00:57、cid 42915138952 |
| `docs/perf/T8.1-online-resolve-mismatch.png` | 串片回显：精灵宝可梦…/02:52；与 OB-1 不同标题 |

截图是离屏 `QWidget::grab()`，只用于证据，不是正式 UI 验收；真人鼠标操作未单独手测。

---

## 9. 未覆盖 / 缺口

1. **未做 T8.2**：没有 nav 自检、没有 SESSDATA/DPAPI、没有 Cookie 注入；WinHTTP 动态客户端只是 T8.1 的最小可替换实现，T8.2 自检后决定产品栈。
2. **未做 T8.5/T8.9**：没有 playurl、档位、DASH、下载、缓存；确认只发信号。
3. **真实 B 站请求中取消**未单独注入（见 §4）；本机慢服务器已覆盖同一 WinHTTP 客户端。
4. **真人手测**未单独做；`--online-resolve-test` 走产品对话框与按钮，截图供视觉核对。
5. **多P live 样本只有一条**（`BV1fNnv6EEon`，6P）；分P 语义另有 6 个 fixture 的单测。
6. **OB-5 的 `-400` 用替代样本**（`BV0000000000`）覆盖；`BV1`/少一位按 O1-A2 本地拒绝，未发请求。
7. 单机、单时段、匿名；网络 RTT 65–230ms，不能外推为性能承诺；T8.1 不测起播/P7 在线指标。
8. 未测代理/TLS 异常；T8.2 的 HTTP 栈自检会覆盖。
9. 未覆盖大会员/4K/60fps；本任务不写这些验收。
