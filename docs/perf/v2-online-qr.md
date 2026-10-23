# T8.2.2 证据：扫码登录（2026-10-23）

- **状态**：**T8.2.2 代码/Fake/回退/UI 完成**；**真实扫码未验证**（没有用户手机配合）。**非 M1/M2 门**；不影响里程碑判定。
- **范围**：`passport.bilibili.com` 官方网页二维码 `generate`/`poll`；二维码**本地渲染**；状态机等待/已扫/过期/成功 + 失败/取消；180s 过期可刷新；成功后只提取 SESSDATA（DPAPI）+ nav 自检通过才显示已登录；SESSDATA 粘贴入口永久保留。
- **不做**：账号密码/注册/个人主页/多账号、WebView/QtNetwork/新 DLL、浏览器 Cookie 读取、后台轮询、风控绕过、真实扫码伪造。
- **代码**：`src/online/QrLogin.*`（URL/JSON/状态机/SESSDATA 提取）、`QrCode.*` + `third_party/qrcodegen`（Nayuki MIT）、`QrLoginController.*`（worker/QTimer/180s/取消/nav 门）、`OnlineResolveDialog`（扫码按钮/面板/回退粘贴）、`--online-qr-test`、`tests/online_qr_test.cpp`。
- **依据**：[tasks-v2.md](../tasks-v2.md) T8.2.2、[spec-v1.0-delta.md](../spec-v1.0-delta.md) O8/O9/§6.1/DEC-6、[T8.2 交接](../handoff-2026-10-23-t8.2.md)、[product-v2-vision.md](../product-v2-vision.md) §3/§5。

---

## 1. 官方接口实测（匿名，未扫码）

用产品 WinHTTP 栈/本机 curl 实测（2026-10-23）：

```text
GET https://passport.bilibili.com/x/passport-login/web/qrcode/generate   # 无 Cookie
GET https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=<32>
```

| 项 | 实测 |
|---|---|
| generate HTTP | 200；`code=0`；`message=OK` |
| generate `data` 字段 | `url`（**https**，host `account.bilibili.com`，长度 131）、`qrcode_key`（32 字符） |
| 请求 Cookie | **无**（`request_cookie_sent=0`） |
| poll（未扫码） | HTTP 200；根 `code=0`；`data.code=86101`；`data.url` 空 |
| poll `data` 字段 | `url`/`refresh_token`/`timestamp`/`code`/`message` |
| poll 响应头 | 有 `Set-Cookie`（成功时）；未扫码响应不含 SESSDATA |

- 轮询 key/URL 查询串**不打印**；日志只留 `host=passport.bilibili.com path=/x/passport-login/web/qrcode/{generate,poll}`。
- `WinHttpClient` 只对 `passport.bilibili.com` 的 HTTPS 响应读取 `Set-Cookie`，**只**提取 `SESSDATA` 并校验安全字符；其它 cookie 字段不进入 `HttpResponse`，不建 cookie jar、不跟随重定向、不向 CDN 带 Cookie。

## 2. 状态机

| 状态 | 进入条件 | 可迁移 | UI 文案 |
|---|---|---|---|
| idle | 初始/Reset | generating | — |
| generating | Start/Refresh | waiting / failed / cancelled | 正在生成二维码… |
| waiting | generate 成功 | scanned / expired / success(→verifying) / failed / cancelled | 请使用 B 站手机客户端扫码 |
| scanned | poll 86090 | success(→verifying) / expired / failed / cancelled | 已扫码，请在手机上确认登录 |
| verifying | poll 0 且拿到 SESSDATA | success / failed / cancelled | 扫码成功，正在自检登录状态… |
| expired | poll 86038 / 180s | generating（刷新） | 二维码已过期，请点击刷新 |
| success | nav `code=0/isLogin=true` + DPAPI 保存成功 | generating（重新扫码） | 已登录 |
| failed | 412/429/坏 JSON/nav 失败/保存失败 | generating（重试） | 可读错误 + 回退粘贴 |
| cancelled | 用户取消 | generating（重新扫码） | 扫码登录已取消 |

- 成功/取消/过期后**不再 poll**；取消/失败/过期清空二维码 key/URL/SESSDATA。
- 成功前不显示“已登录”：poll 只到 `verifying`，nav 通过才 `success`。
- 默认过期 **180s**（`kQrLoginExpiryMs`）、轮询 **1.5s**（`kQrPollIntervalMs`）；测试可缩短。

## 3. QR 本地渲染

- vendored **Project Nayuki QR-Code-generator**（MIT，commit `3c6d0b3cefb4e049dc337e82237c9644399716a8`，2026-08-31），`src/online/qrcodegen/qrcodegen.{hpp,cpp}`；许可 `licenses/qrcodegen.txt` + `THIRD-PARTY-NOTICES.md`。
- 无 WebEngine/QtNetwork/新 DLL；静态编译进主程序；不加载远程图片/字体/统计。
- 渲染：`QrCode::encodeText`（ECC MEDIUM）→ `QImage` RGB32 → 整数缩放 → 4 模块 quiet zone；失败返回空图 + 可读错误。
- 证据（官方 URL 文本）：`qr_version=4`、`qr_modules=33`、`qr_image_px=246`（33+8=41 模块 × 6px）、`qr_quiet_zone=4`；左上/右上/左下 finder pattern 断言通过；空文本/超长文本拒绝。
- **未做独立解码器验证**：结构断言 + 上游成熟 encoder；没有引入 ZXing/OpenCV 等额外依赖。

## 4. 测试与数字

| 项 | 结果 |
|---|---|
| `ctest` | **28/28**（30.13s；新增 `lapian_online_qr_test`） |
| `lapian_online_qr_test` | **115 checks / 0 failures** |
| `--online-qr-test`（build） | **19 / 0**，`verdict=pass` |
| `--online-qr-test --ui`（build，真实对话框不 show） | **21 / 0**，`ui_smoke=pass` |
| `--online-qr-test`（dist） | **19 / 0**，`verdict=pass` |
| `--online-qr-test --ui`（dist） | **21 / 0**，`ui_smoke=pass` || `--online-qr-test --real-generate`（build/dist） | `real_generate=pass`、`real_cookie_sent=0`、key 32、URL https、poll `data_code=86101`/waiting；`verdict=pass` |
| 真实扫码（1 次） | **未验证**（无用户手机）；`real_scan=not_verified`，不写 O8-A8 通过 |
| 取消 | **16ms**（build 一次）/ 0ms（UI 一次）；状态 cancelled；200ms 内无新 poll |
| 过期 | 250ms（测试缩短）→ expired；poll 停止；刷新重新 generate、新 key、旧 key 清空；默认 180s 由常量断言 |
| 4 态 | `state_sequence=generating,waiting,waiting,scanned,verifying,success`（URL 与 Cookie-only 两路） |
| 只保留 SESSDATA | 成功 URL 含 `SESSDATA`/`bili_jct`/`refresh_token`/`DedeUserID`；DPAPI 载入只有 SESSDATA（`FAKE_SESSDATA_T822_SENTINEL`） |
| nav 门 | nav `code=0/isLogin=1` → success；nav `-101` → failed、不保存凭据、不显示已登录 |
| 412/429 | generate 412 / poll 429 → failed；429 只 poll 1 次，不循环 |
| 坏 JSON | generate 坏/ poll 坏 → failed，可读错误，不崩 |
| QR 请求 Cookie | 所有 QR generate/poll 请求 `sessdata` 为空；`request_cookie_sent=0`（真实 generate） |
| 日志哨兵 | `FAKE_SESSDATA…`=0、`FAKE_CSRF`=0、`refresh_token`=0、`qrcode_key=`=0（`LAPIAN_ONLINE_LOG=1` 全量日志） |
| 本地零网络 | `--startup-test` + `LAPIAN_ONLINE_LOG=1`：`http_request/http_done/passport/api` 命中 **0** |
| `--online-auth-test` | **54 / 0**（粘贴/nav 不退化） |
| `--online-resolve-test` | **156 / 0** |
| `--online-cache-test` / `--online-growing-test` / `--online-fetch-test` | 32/0、22/0、46/0（均 verdict=pass） |
| 本地 P1/P2 | **274.43ms / 331.18ms** |
| 本地 P7（2s GOP，n=20） | p50 **59.45ms** / p95 **65.20ms** / max **66.35ms** / over **0** / pass |
| `dist` | **33.42MB / 25 文件**（P12 余量 66.58MB） |
| `check-imports` | 20 个 exe/dll 通过；无 QtNetwork/TLS/winhttp/wininet/urlmon；无新 DLL |

**真实扫码**：需要用户手机扫描官方二维码并在手机上确认；本次未执行，证据明确写“未验证”。非门任务不因此失败；fake/回退路径已完成。

## 5. UI 与回退

- `OnlineResolveDialog` 登录区新增：
  - **粘贴 SESSDATA**（原有：输入框、保存、清除、DPAPI、状态）；
  - **扫码登录**按钮（`onlineQrStart`）+ 内嵌面板（`onlineQrImage`/`onlineQrStatus`/`onlineQrRefresh`/`onlineQrCancel`）；
  - 面板显示二维码图（240×240 容器，实际 246px 源图缩放）、状态文字、刷新/取消；
  - 成功：只更新 `BiliAuthSession` 为 logged_in，显示“已登录（扫码）；SESSDATA 已用 DPAPI 保存”；
  - 失败/过期/取消：面板提示“仍可用上方粘贴 SESSDATA”；粘贴入口不删除、不禁用、不降级；
  - 关闭对话框/关窗：未成功则取消并 join；已成功保留凭据。
- UI smoke（`--online-qr-test --ui`）：构造真实 `OnlineResolveDialog`，注入 fake HTTP：等待→面板可见/二维码非空→成功→`AuthState=logged_in`/凭据保存/状态文案；dist 同通过。

## 6. 隐私与边界

- 只发 `passport.bilibili.com` 的 HTTPS generate/poll；仅用户点“扫码登录”/刷新时发；不后台轮询/预取。
- QR generate/poll **不带 SESSDATA**；响应 `Set-Cookie` 只在 passport 请求中解析，只保留 SESSDATA；`bili_jct`/`refresh_token`/`DedeUserID`/昵称/UID 全部丢弃。
- 日志/错误/UI 不含 SESSDATA、qrcode_key、二维码 URL 查询串；真实日志只留 host/path/status/elapsed。
- nav 通过后才显示已登录；nav 失败不保存、不静默降级（提示回退粘贴）。
- 不引入 WebView/QtNetwork/新 DLL；qrcodegen MIT 许可已补；本地模式启动/打开/设置/标记/截图/关闭不请求 passport。

## 7. 已知缺口

1. **真实扫码未验证**（无用户手机）：O8-A8 不写通过；只写“接口实测 + fake/UI/回退完成”。
2. **成功响应字段依赖官方**：真实成功响应可能只在 `Set-Cookie` 或 `data.url`；两条路径都实现，但真实成功未抓包验证（未扫码）。
3. **nav 失败未真实触发**：fake `-101`/超时覆盖；真实无效 SESSDATA 由 T8.2 已有证据。
4. **QR 渲染没有独立解码器**：只做结构/上游 encoder；未用 ZXing/OpenCV 二次解码。
5. **UI smoke 只覆盖扫码成功/状态/凭据**：未自动覆盖 100–200% DPI、全屏、粘贴输入焦点隔离；这些沿用已有 UI 测试/人工。
6. **轮询频率/风控**：1.5s、180s、失败 3 次停；真实风控 412/429 未实测，fake 覆盖。
7. **Windows 通知/扫码 App 版本差异**：官方二维码内容/字段可能变；未知结构 → 可读失败 + 粘贴回退。

## 8. 复现

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
build\release\bin\lapian.exe --online-qr-test
build\release\bin\lapian.exe --online-qr-test --ui          # 真实对话框 smoke（不显示窗口）
build\release\bin\lapian.exe --online-qr-test --real-generate  # 官方字段实测，不扫码/不保存
# 日志哨兵（fake 模式；应 0 命中）
$env:LAPIAN_ONLINE_LOG='1'; build\release\bin\lapian.exe --online-qr-test --ui *> .deps\tmp\qr-log.txt
Select-String -Path .deps\tmp\qr-log.txt -Pattern 'FAKE_SESSDATA|qrcode_key=|refresh_token'
```

## 9. 结论

- 扫码登录链路（官方 generate/poll → 本地 QR → 4 态/180s/取消 → SESSDATA-only → DPAPI → nav 门 → UI）代码/单测/fake/UI smoke/回退均完成。
- **真实扫码 1 次未验证**；不虚构 O8-A8。非门任务交付完成，不影响 M1/M2。
