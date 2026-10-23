# 下一个任务：T8.2.2 扫码登录（v2 在线 · DEC-6 · 非门）

> 给接手 agent 的自包含提示词。目标：**只做 T8.2.2**，做完一个可审查提交。
> 本任务**不是 M1/M2 门**；失败不得影响里程碑判定。不要顺手做 T8.5 取流、T8.6 会话 UI、T8.7 记录。
> 当前仓库：`master`，HEAD 为 `44bba88`（T8.4）或其后文档提交；工作树 clean；`ctest` **26/26**（若 T8.5 已合并则 **27/27**）。

---

## 0. 一句话

在现有 SESSDATA 粘贴入口旁增加 **B 站官方网页二维码登录**：用户点“扫码登录”才请求 `passport.bilibili.com` 的 `qrcode/generate` + `qrcode/poll`，二维码**本地渲染**，状态机“等待扫码/已扫码待确认/已过期/成功/失败”，180s 过期可刷新、可取消；成功后**只提取 SESSDATA**，复用 `CredentialStore`（DPAPI）+ `BiliAuth::CheckNav`，**nav 自检通过后才显示“已登录”**。粘贴入口永久保留。

## 1. 先读（按顺序，不要凭记忆）

1. `AGENTS.md` v6.16：§1 v2 状态、§7 明确不做、§11 协作规则。
2. `docs/spec-v1.0-delta.md` v0.4：**O8**（凭据/隐私/扫码）、**O9**（本地零网络）、**§6.1 网络白名单**、**§9.0 DEC-6**、DEC-3（首次在线 nav 自检）。
3. `tasks-v2.md` v0.8：**T8.2.2 行**（验收列=DoD）；T8.2 行（HTTP/nav/凭据接口）。
4. `docs/product-v2-vision.md` v1.1 §3/§5：扫码是“傻瓜化”增强，不引入 WebView/账号密码/多账号。
5. `docs/handoff-2026-10-23-t8.2.md` §1.3/§2/§3：`IHttpClient`、显式 SESSDATA、`CredentialStore`、`BiliAuthSession`、脱敏、门证据。
6. `docs/handoff-2026-10-23-t8.4.md` §6：读层边界（本任务不碰）。
7. 代码：`src/online/IHttpClient.h`（**目前只有 GET + body，无 response headers/Set-Cookie**）、`WinHttpClient.h/.cpp`（动态加载、无 cookie jar、不跟随重定向、Cookie 只发 `api.bilibili.com`）、`CredentialStore.h`、`Sessdata.h`、`BiliAuth.h`、`src/app/OnlineResolveDialog.h/.cpp`（粘贴 UI/DE​​C-3）。
8. `licenses/THIRD-PARTY-NOTICES.md`、`licenses/SOURCE-OFFER.md`：新增 vendored QR 库必须补许可。

## 2. 当前状态

| 任务 | 状态 | 与本任务关系 |
|---|---|---|
| T8.2 HTTP/凭据 | ✅ | `IHttpClient`/WinHTTP、SESSDATA/DPAPI、nav 自检、粘贴 UI 已有；本任务复用 |
| T8.4 读层 | ✅ | 不相关，不要改 `GrowingSource` |
| T8.5 durl 取流 | 可能并行/未完成 | **不要**改其 `PlayUrl`/`DownloadJob`/`IHttpClient` 下载扩展；若同工作树，先做 T8.5 |
| T8.2.2 本任务 | 待做 | 非门；可独立于 T8.5 完成 |

**并行警告**：T8.2.2 与 T8.5 都可能改 `IHttpClient`/`WinHttpClient`/`OnlineResolveDialog`/`CMakeLists.txt`/`AGENTS.md`/`tasks-v2.md`。**推荐串行：先 T8.5，再本任务**。若必须并行，使用独立 git worktree/分支；本任务先做纯状态机/QR 渲染，等 T8.5 HTTP 下载接口稳定后再接 UI，最后合并时人工解冲突。

## 3. 范围

### 做

- `src/online/QrLogin.h/.cpp`（纯逻辑，便于 fake 单测）：
  - URL 构造：`BuildQrGenerateUrl()`、`BuildQrPollUrl(qrcode_key)`。
  - JSON 解析：generate → `qrcode_key`、`qr_url`（实际字段名以真实响应为准）；poll → `code`/`message`/成功 `data.url`（含 SESSDATA）或 Set-Cookie。
  - 状态机：`idle/generating/waiting/scanned/expired/success/failed/cancelled`；只允许合法迁移；成功/取消/过期后不再 poll。
  - 只提取 SESSDATA：从 `data.url` 查询参数或 `Set-Cookie` 中取 `SESSDATA`；`NormalizeSessdata` 校验；其余 cookie/refresh_token/昵称/UID **全部丢弃**。
  - 错误映射：412/429/风控、网络、超时、取消、坏 JSON、未知 code；可读中文，不含二维码 key/URL 查询串。
- `src/online/QrCode.h/.cpp`（或 `app/QrCode`）：
  - vendored **MIT** 单文件 QR 编码库（例如 Nayuki `QrCode` 风格）；**不引入 WebEngine/QtNetwork/新 DLL**；补 `licenses/<lib>.txt` + `THIRD-PARTY-NOTICES.md`。
  - `RenderQrPng/QImage(text, pixel_size)`：高对比、留 quiet zone、整数缩放；失败返回空图 + 可读错误。
- `src/app/QrLoginController.h/.cpp`（或并入 `OnlineResolveDialog`）：
  - 一个工作线程执行 generate/poll；UI 用 `QTimer` 1–2s 轮询一次；不阻塞 UI。
  - `Start/Refresh/Cancel/Close`；`Cancel` 置 `HttpCancelToken` 并 join，**≤100ms** 返回（测量）。
  - 180s 过期（从 generate 成功起算）；过期停止 poll，显示“已过期，点击刷新”；刷新重新 generate，旧 key 丢弃。
  - 成功：提取 SESSDATA → `CredentialStore::Save`（DPAPI）→ 在 worker 跑 `CheckNav` → nav 通过才 `BiliAuthSession::CompleteCheck`/UI“已登录”；nav 失败显示“登录不可用”，不显示假登录。
- `OnlineResolveDialog`：在粘贴入口旁加“扫码登录”按钮 + 最小二维码对话框/内嵌面板（状态文字、刷新、取消、回退“粘贴 SESSDATA”）；**粘贴入口不删除、不降级**；成功后同步 `auth_`/状态标签。
- `src/app/OnlineQrTestRunner.h/.cpp` + `main.cpp` 分流 + `src/CMakeLists.txt`：
  - `--online-qr-test`：无 GUI/无真实账号；fake `IHttpClient` 队列响应；输出 key=value + `verdict`；覆盖 4 态、过期、取消、坏 JSON、412/429、只提取 SESSDATA、日志无凭据、QR 渲染尺寸/可解码性。
- 单测 `tests/online_qr_test.cpp` + ctest `lapian_online_qr_test`（新增 1 个；基线 26→27 或 27→28）。
- 证据 `docs/perf/v2-online-qr.md` + 交接 `docs/handoff-YYYY-MM-DD-t8.2.2.md`。

### 不做

- ❌ T8.5 `playurl`/CDN/下载；T8.6 会话 UI/播放；T8.7 记录；T8.9 DASH。
- ❌ 账号密码/注册/个人主页/多账号/云同步；浏览器 Cookie 读取/注入；WebView/WebEngine。
- ❌ 自动后台轮询/预取；用户不点“扫码登录”不发请求。
- ❌ 在成功前显示“已登录”；保存昵称/UID/头像/refresh_token。
- ❌ 绕过风控/验证码/412/429；不模拟客户端签名。
- ❌ 修改 `spec.md`/`tasks.md`/`plan.md` v1 正文、M1/M2 门/数字。

## 4. 二维码接口契约（**先实测再定字段**）

```text
GET https://passport.bilibili.com/x/passport-login/web/qrcode/generate
    # 不带 SESSDATA；只应在用户点“扫码登录”时调用
GET https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=<key>
    # 不带 SESSDATA；轮询间隔 1–2s；180s 上限；成功/取消/过期后停止
```

- generate 成功：`code==0`；`data.url`（二维码内容/官方登录 URL）、`data.qrcode_key`（轮询 key）。
- poll 的 `data.code`（官方语义，实测确认）：
  - `0` 成功；成功响应可能在 `data.url` 查询串里带 `SESSDATA=...`，也可能只在 `Set-Cookie` 里；**以真实响应为准**。
  - `86101` 未扫码；`86090` 已扫码待确认；`86038` 已过期。
  - 其它/未知/`412/429` → 失败/风控，给可读提示，不重试到成功。
- **当前 `IHttpClient::HttpResponse` 没有 headers/Set-Cookie**：
  - 优先解析成功响应的 `data.url` 查询参数 `SESSDATA`（不新增 header 暴露面）。
  - 若真实响应只给 `Set-Cookie`，再加**最小** `HttpResponse::set_cookie`/`response_headers`（只在 `passport.bilibili.com` 请求上填充；只提取 SESSDATA 后立即丢弃；不打印）。
  - **不要**把 Cookie jar、redirect、账号信息引入 `WinHttpClient`。
- 二维码 key/URL 查询串不是 SESSDATA，但也不入日志；日志只留 host/path/status/elapsed。
- 主机白名单：`passport.bilibili.com`（仅 QR generate/poll）。**CDN/其它 host 不带 Cookie**。

## 5. DoD（tasks-v2 T8.2.2 验收列，逐条证据）

| # | 验收 | 方法 | 通过条件 |
|---|---|---|---|
| 1 | fake HTTP 单测：4 态 | 等待/已扫待确认/过期/成功 | 状态迁移正确；成功前不显示已登录 |
| 2 | 过期 | fake 86038 / 180s 计时 | 停止 poll；显示已过期；刷新可重新 generate；旧 key 丢弃 |
| 3 | 取消 | 等待/已扫/轮询中 `Cancel` | ≤100ms 回可操作；不再 poll；保留粘贴入口 |
| 4 | 坏 JSON | 空/截断/字段类型错 | 可读错误；不崩；不显示已登录 |
| 5 | 412/429/风控 | fake 状态/业务码 | 失败提示；不自动重试到成功；回退粘贴 |
| 6 | 只提取 SESSDATA | 成功响应含多 Cookie/refresh_token/昵称 | 磁盘/日志/UI 只有 SESSDATA；无其它字段 |
| 7 | 日志/弹窗无凭据 | 哨兵 SESSDATA + QR key | 任何日志/弹窗/异常文本不含哨兵/完整 Cookie/查询串 |
| 8 | nav 通过才登录 | fake nav `code=0/isLogin=1`；另测 -101/超时 | 通过→已登录；失败→“登录不可用”，按匿名/粘贴处理 |
| 9 | 真实扫码 1 次 | 用户手机扫官方二维码 | 成功且 nav 通过；只保存 SESSDATA；**没有手机/用户配合就写“未验证”，不伪造** |
| 10 | 回退粘贴 | 过期/取消/失败/风控后 | 粘贴入口仍可用；两入口长期并存 |
| 11 | 本地零网络 | 启动/本地打开/设置/标记/截图/关闭 | 0 请求；不初始化 `passport`/网络栈（O9） |
| 12 | 无新依赖/导入 | `check-imports` + `dist` | 无 WebEngine/QtNetwork/新 DLL；许可完整 |

**非门**：即使真实扫码未做，代码/fake 测试/回退路径完成也可交付；但证据必须写“真实扫码未验证”，不得写 O8-A8 通过。

## 6. 实现顺序

1. **纯 QR 编码/渲染**：vendored MIT 库 + `QrCode` wrapper；单测固定文本的矩阵尺寸/quiet zone/像素非空；补许可。
2. **纯状态机/JSON**：`QrLogin` + fake `IHttpClient`；先不接 UI，确保 4 态/过期/取消/坏 JSON/风控/只提取 SESSDATA；日志哨兵测试。
3. **HTTP**：用现有 `WinHttpClient` GET（无 SESSDATA）访问 `passport`；需要 `Set-Cookie` 时做最小 header 扩展；确认无 redirect/cookie jar；确认 `request_cookie_sent=false`。
4. **Controller**：worker + QTimer 1–2s；180s deadline；`Cancel` join ≤100ms；成功后 `CredentialStore::Save` + `CheckNav` worker；UI 只在 nav 通过后 logged_in。
5. **UI**：`OnlineResolveDialog` 加“扫码登录”按钮 + QR 面板（QImage/QLabel）、状态、刷新、取消、回退提示；粘贴仍可用；清除凭据同步。
6. **探针/测试**：`--online-qr-test` 无 GUI；fake HTTP 全场景；QR 渲染断言；日志哨兵。
7. **真实扫码**：有用户手机时走一次；记录时间/状态/仅 SESSDATA；没有则 blocked。
8. **回归/证据/文档/commit**。

## 7. 测试与命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
build\release\bin\lapian.exe --online-qr-test
```

- `ctest` 基线 26/26（T8.5 后 27/27）；本任务新增 1 个 → **27/27 或 28/28**。
- 回归：`--online-auth-test` 54/0（粘贴/nav 不退化）、`--online-cache-test` 32/0、`--online-growing-test` 22/0、`--online-resolve-test` 156/0（联网，可选）；本地 P1/P2/P7；`dist`/`check-imports`。
- `--online-qr-test` 必须无 GUI、无真实账号、无网络；输出 `checks/failures/verdict` 与状态统计。
- 真实扫码：运行产品（`build\release\bin\lapian.exe`），打开在线对话框 → 扫码登录 → 手机扫 → 确认；记录“成功/失败/未验证”，**不截图二维码/SESSDATA**。

## 8. 证据与文档

- `docs/perf/v2-online-qr.md`：接口实测字段、状态机表、fake 场景矩阵、QR 渲染尺寸、取消/过期耗时、真实扫码结果（或“未验证”）、日志哨兵、回归。
- `docs/handoff-YYYY-MM-DD-t8.2.2.md`：接口、状态机、测试数字、缺口、T8.5/T8.6 影响（应为无）。
- `tasks-v2.md`：T8.2.2 完成/未验证（非门）；版本升 v0.9 或 v1.0（按 T8.5 是否已做；不要覆盖 T8.5 状态）。
- `AGENTS.md`：v6.17/6.18（按顺序）；文档表加 QR 证据/交接/提示词。
- 一个 commit；工作树 clean；不要提交 `.secrets`/二维码截图/真实 SESSDATA。

## 9. 已知坑

1. **官方响应字段会变**：`generate`/`poll` 的 `data.url`/`qrcode_key`/成功 Cookie 位置必须先实测；不要按记忆写死。未知字段/结构变化 → 可读失败，不猜。
2. **`IHttpClient` 没有 headers**：成功 SESSDATA 可能在 `Set-Cookie`；扩展必须是**最小、仅 passport、只提取 SESSDATA**，不要通用 Cookie API。
3. **`WinHttpClient` 的 Cookie 白名单只允许 `api.bilibili.com`**：QR 请求不要设置 `sessdata`，否则会被拒绝/泄漏逻辑混乱；确认 `request_cookie_sent=false`。
4. **轮询不能太密**：1–2s；180s 上限；成功/取消/过期立即停；离开页面/关窗 `Cancel` + join；不后台轮询。
5. **成功前不得显示已登录**：poll 成功只是拿到 SESSDATA；必须 nav 通过后才 `logged_in`。nav 失败保留粘贴/匿名，明确提示。
6. **只保留 SESSDATA**：`refresh_token`/`buvid`/`DedeUserID`/昵称/UID 全部丢弃；不写 meta.json；不打印。
7. **二维码库许可**：必须 MIT/兼容；补 `licenses/` 与 `THIRD-PARTY-NOTICES.md`；不能新增 DLL/QtNetwork/WebEngine；`check-imports` 必须不变。
8. **本地零网络**：QR 代码不能远程加载图片/字体/统计；不点扫码不发请求；启动/本地模式 0 网络。
9. **取消 ≤100ms**：WinHTTP 请求在工作线程；`Cancel` 后 `HttpCancelToken` 轮询/关句柄；UI 不 join 到卡死（可异步等待，但状态必须 ≤100ms 可操作）。
10. **真实扫码依赖用户手机**：没有用户配合就写“未验证”；不能伪造 O8-A8；非门不影响 M1/M2。
11. **两入口并存**：不要删除/弱化粘贴；扫码失败/无手机/风控必须回到粘贴。
12. **并行冲突**：与 T8.5 同改 `IHttpClient`/`OnlineResolveDialog`/`CMakeLists`/`AGENTS.md`/`tasks-v2.md`；独立 worktree 或串行。

## 10. 停止条件

1. 无用户手机/无法真人扫码：完成 fake/代码/回退，真实扫码写“未验证”，不宣称 O8-A8 通过。
2. 官方 QR 接口变更/412/429/需要验证码：不绕过、不模拟签名；给可读失败 + 粘贴回退。
3. 成功响应无法在不扩大 Cookie 暴露面的前提下取到 SESSDATA：先停下来报告响应结构，评估最小 header 方案，不要临时加通用 cookie jar。
4. QR 库许可不兼容/需要新 DLL/WebEngine：停止，换 MIT 单文件方案。
5. 无法保证取消 ≤100ms 或本地零网络：停止并报告，不要用“大概”写通过。

## 11. 交付清单

- [ ] vendored MIT QR 库 + 许可 + `QrCode` 渲染
- [ ] `QrLogin` 状态机/JSON/SESSDATA 提取
- [ ] `QrLoginController` + `OnlineResolveDialog` 扫码入口（粘贴保留）
- [ ] `--online-qr-test` + `lapian_online_qr_test`
- [ ] `docs/perf/v2-online-qr.md` + `docs/handoff-*-t8.2.2.md`
- [ ] `tasks-v2.md`/`AGENTS.md` 状态同步（非门/真实扫码未验证要写清）
- [ ] ctest = 基线+1；`--online-qr-test` pass；auth 54/0；本地 P1/P2/P7；dist/check-imports
- [ ] 工作树 clean，一个 commit
