# 下一个 agent 提示词：v2 在线 T8.2（HTTP 栈自检 + 凭据）

> 直接把下面整段复制给新会话。日期：2026-10-23。工作目录：`E:\拉片软件开发`。

---

你是拉片软件 v2 在线模式的实现 agent。**本次只做 T8.2：HTTP 栈自检 + SESSDATA 凭据 + DPAPI + 日志脱敏 + DEC-3 自检时机**。不做 T8.3 缓存、T8.4 可增长读层、T8.5 取流/播放、DASH、4K/60fps 验收，也不做 UI 大改。

## 先读（按顺序）

1. `AGENTS.md` §1 v2 条目、§11 协作规则、§12 文档表。
2. `docs/spec-versioning.md` §1：v1 冻结 / v2 增量边界。
3. `docs/spec-v1.0-delta.md`：§2.8 O8 凭据与隐私、§2.9 O9 本地零网络、§5 失败矩阵、§7 隐私、§9.0 DEC-1…5（重点 DEC-3）、§9.1 B 条件验收（只读，不是 T8.2 范围）。
4. `plan.md`：§12.1 组件边界、§12.2 线程/并发、§12.9 HTTP、§12.13 风险；只读，不改 v1 正文。
5. `tasks-v2.md`：T8.2 行 + “任务完成定义” + “审核门”。
6. `docs/perf/v2-online-spike.md`：§3.5 HTTP 栈对照、§5 失败码；硬事实是**同一 SESSDATA：curl 通过，.NET -101**。
7. `docs/adr/ADR-007-bilibili-online.md` 的“附条件接受”评审记录。
8. `docs/handoff-2026-10-23-t8.1.md` 与 `docs/perf/v2-online-resolve.md`：T8.1 的接口、数字、缺口。

## 当前已确定的事实（不要重做）

- T8.1 解析层已完成：BV/URL → 匿名 `view` → 标题/UP/时长/分P → 回显确认；`ctest` 23/23，产品探针 156/0，dist 156/0；证据在 `docs/perf/v2-online-resolve.md`。
- 已有 `IHttpClient`（`src/online/IHttpClient.h`）、`WinHttpClient`（动态加载 `winhttp.dll`，匿名 GET，`WINHTTP_DISABLE_COOKIES`）、`BiliResolveClient`、`OnlineResolveDialog`。
- **`HttpRequest` 还没有 headers 字段；没有 Cookie/DPAPI/nav；T8.2 负责补。**
- T8.1 的 WinHTTP 是可替换最小实现，不是产品栈最终决定；T8.2 必须用产品实际栈做带 SESSDATA 的 `nav` 自检，WinHTTP 优先，libcurl 备选。
- v1 本地模式运行期零网络请求；`scripts/check-imports.ps1` 禁止 `WINHTTP.DLL`/QtNetwork/TLS 导入。当前 WinHTTP 靠 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 动态加载，dist 才没有 `WINHTTP.DLL` 导入。
- 已拍板：M1→M2；起播 p95 ≤5s / 已缓存 P7 ≤80ms / 缓存命中 ≤2.5s / 未缓存提示取消 ≤100ms / 任务完成率 ≥80%；DEC-3 是“首次在线打开（有凭据时）自检一次，会话内缓存；不在启动”。B 的 1080P60/4K 是 T8.15 条件验收，不进 T8.2/M1/M2 门。

## T8.2 目标（DoD）

1. **HTTP 头/Cookie 能力**：给 `HttpRequest` 增加最小 `headers`（或等价的显式 Cookie 参数）；只允许显式 `Cookie: SESSDATA=...`，不做自动 Cookie jar、不做多账号。保持 WinHTTP 的 `WINHTTP_DISABLE_COOKIES`，手动加头。
2. **`CredentialStore`**：只收 SESSDATA；`%APPDATA%\拉片软件\credentials.dat`；Windows DPAPI 当前用户加密（`CryptProtectData`/`CryptUnprotectData`，禁止 `CRYPTPROTECT_LOCAL_MACHINE`）；原子写；`Load/Save/Clear/Has`；损坏文件按“无凭据”处理，不崩。
3. **`nav` 自检**：GET `https://api.bilibili.com/x/web-interface/nav`，带 `Cookie: SESSDATA=...`；解析 `code`、`data.isLogin`；只有 `code=0 && data.isLogin==true` 才算通过；记录耗时/HTTP 状态/错误类别。
4. **失败映射**：`-101` → “登录不可用/凭据失效；将按未登录清晰度播放”；超时/DNS/TLS/412/429 有可读消息；**不静默降级、不假装已登录**。
5. **DEC-3 时机**：有凭据时，**用户首次提交 BV/URL 后**自检一次，会话内缓存；不要在启动、本地打开、设置、标记、截图、关闭时联网；不要在只打开在线对话框、还没提交 BV 时就发 `nav`（O8-A4 与“首次在线打开”要同时满足，按“首次提交”实现并写清解释）。`nav` 失败不应让 `view` 解析失败：继续匿名解析，界面明确“登录不可用”。
6. **凭据 UI（最小）**：粘贴 SESSDATA（密码框）、保存、清除、当前状态；不做扫码/账号密码/账号体系。首次在线使用显示隐私说明（会联网、Cookie 用途、缓存位置、如何清除）；本地模式不显示。
7. **日志/错误脱敏**：不打印 Cookie/SESSDATA/完整查询串；请求日志只保留 host/path/status/elapsed；哨兵 Cookie 不得出现在日志、弹窗、崩溃文本、meta。
8. **`--online-http-selfcheck`**：无 GUI、产品实际栈；支持 `winhttp` / `libcurl`（若构建）/`auto`；从 `.secrets\bilibili_sessdata.txt` 或 `LAPIAN_SESSDATA_FILE` 指定文件（**不要用命令行参数传 secret**）；输出 `stack=... code=... is_login=... elapsed_ms=... http_status=... error=...`；至少一种产品实际栈 `code=0/isLogin=true` 才算过门。
9. **测试/证据**：DPAPI 往返、无明文、损坏回退、清除后匿名；哨兵 Cookie 扫描；nav 成功/`-101`/超时/取消；本地 P1/P2/P7 与 0 网络回归；写 `docs/perf/v2-online-http-selfcheck.md` 与 T8.2 交接。

## 硬约束

- **门**：至少一种产品实际栈 `nav code=0/isLogin=true`。两种都 `-101`/都不可用 → **停线重决策**，写 blocked 交接，不要用匿名 `view` 冒充通过，不要写“登录可用”。
- Cookie 只发 `api.bilibili.com`；不得发 CDN。`nav` 不跟随跨域重定向；若跟随，必须清 Cookie 或直接禁止重定向。抓包/代理日志证明。
- TLS 证书校验必须开启；禁止 `-k`/关闭校验的“临时调试”进入产品路径。
- 超时/取消可用；`nav` 建议 ≤2s 超时，失败继续匿名；不要因为登录自检把 `view` 的 100ms 本地/网络错误路径拖成 5s。
- 不把 SESSDATA 写进 URL、日志、异常、JSON meta、`index.jsonl`、截图文件名；`.secrets/` 不入库。
- 不改 `spec.md`/`tasks.md`/v1 冻结文件；不改 delta 验收数字、M1/M2 门；不把 4K/1080P60 写成 MVP 通过；不写未缓存区 80ms。
- 不做 T8.3 缓存、T8.4 `GrowingSource`、T8.5 `playurl`/播放；T8.2 的 `HttpClient` 只做 JSON/headers/nav，不要顺手做流式下载。
- 不破坏 T8.1：`--online-resolve-test` 156/0、匿名 view、非法输入 0 请求、`ctest` 23/23、dist 导入表白名单都要回归。
- 不把 WinHTTP 写死成最终产品栈；若选 libcurl，必须记录 DLL/许可/体积影响，T8.16 再处理打包，T8.2 先保证本地构建与证据。

## 账号 / 环境

- 普通用户（非大会员）账号的 SESSDATA，放 `.secrets\bilibili_sessdata.txt`（单行；不提交；不回显）。
- 大会员账号只用于 T8.15，**不要用大会员账号冒充 T8.2 普通用户通过**。
- 可访问 `api.bilibili.com` 的 HTTPS；能跑 `curl.exe` 做对照（同一 Cookie：curl 通过 / .NET -101）。
- 最好能抓包或用本地代理，证明 Cookie 只去 `api.bilibili.com`、CDN 不带 Cookie；至少要有请求头/日志/计数证据。
- 如果拿不到账号：不要伪造；写“blocked：缺普通用户 SESSDATA”，列出已完成的匿名/DPAPI/脱敏部分，不要标 T8.2 完成。

## 实施建议（可调整，但要保持小步）

- 新增 `src/online/CredentialStore.h/.cpp`、`src/online/BiliAuth.h/.cpp`（或等价命名）；`CredentialStore` 只做 DPAPI 存储，`BiliAuth` 只做 nav/状态/错误映射，不解析 `view`。
- `HttpRequest` 增加 `std::vector<HttpHeader>` 或最小 `cookie` 字段；`WinHttpClient` 拼头时只允许 `Cookie` 和 `User-Agent` 这类白名单，拒绝 CR/LF 注入。
- `WinHttpClient` 请求日志绝不打印 headers/query；错误信息用分类（`dns`/`tls`/`timeout`/`http_412`/`api_-101`）。
- 状态建议：`anonymous` / `not_checked` / `checking` / `logged_in` / `login_unavailable`；会话内缓存只缓存“已检查”结果，清除凭据立即回 `anonymous`。
- `OnlineResolveDialog` 只加最小状态/凭据入口；不要在对话框首次显示时联网。首次提交时若 `Has()`，先/并行 nav，失败继续 view。
- `--online-http-selfcheck` 放在 `src/app/OnlineHttpSelfCheckRunner.*` 或复用 `main.cpp` 的探针模式；测试 secret 用临时文件/环境变量，结束删除。
- 单测：DPAPI roundtrip、plaintext 扫描、损坏文件、清除、sentinel log、fake nav `-101`、超时、cancel；真实 nav 作为可选/手测入口，不把真实账号塞进 ctest。
- 证据必须写清：栈名、DNS/TLS/TTFB、HTTP 状态、API code、isLogin、耗时、失败类别、Cookie 去向、TLS 校验状态、代理环境。

## 构建 / 测试命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"

cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

build\release\bin\lapian.exe --online-http-selfcheck --http-stack=winhttp
build\release\bin\lapian.exe --online-resolve-test
build\release\bin\lapian.exe --startup-test --file media\proxy\proxy-V1.mp4
build\release\bin\lapian.exe --gop-boundary-test --file build\test-media\real-gop2s-full.mp4 --samples 20

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -SkipTests
dist\lapian.exe --online-resolve-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1
```

## 交付

- 代码/测试/`--online-http-selfcheck`。
- `docs/perf/v2-online-http-selfcheck.md`：命令、栈、账号类型（只写“普通用户”，不写账号）、样本、成功/失败表、耗时、Cookie 去向、TLS/代理、缺口。
- `docs/handoff-YYYY-MM-DD-t8.2.md`：改了什么、接口、测试数字、缺口、T8.3/T8.5 需要。
- 更新 `tasks-v2.md` T8.2 状态；更新 `AGENTS.md` v2 状态/文档表；**不改 M1/M2 门与 delta 数字**。
- 最终报告按 5 段：改动文件/接口；测试命令/结果；证据路径；未解缺口；T8.3/T8.5 需要什么。

## 停止条件（任一出现就停，不要猜）

1. 两种产品栈都 `-101` 或都拿不到 `nav` 成功；
2. `nav` 需要登录/签名/WBI 之外的新条件，和 S2 记录不一致；
3. Cookie 无法限制在 `api.bilibili.com`，或 TLS 校验无法开启；
4. 账号/网络不可用，无法完成门；
5. 发现实现必须改 O9 本地零网络或 M1/M2 验收数字。

遇到上述情况：写 blocked 证据与已完成的匿名/DPAPI/脱敏部分，明确“T8.2 未通过”，然后问用户；不要用匿名 `view`、假 `isLogin`、mock 结果过门。
