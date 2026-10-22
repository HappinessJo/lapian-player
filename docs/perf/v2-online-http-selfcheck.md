# T8.2 HTTP 栈自检与凭据证据（v2 在线）

- **版本**：v1.0（2026-10-23）
- **状态**：**T8.2 完成**；门已通过（WinHTTP 产品栈 nav `code=0/isLogin=true`）。
- **范围**：HTTP 头/SESSDATA、DPAPI 凭据、nav 自检、DEC-3 时机、日志脱敏、`--online-http-selfcheck`。**不含** T8.3 缓存、T8.4 可增长读层、T8.5 取流/CDN、DASH、4K/60fps。
- **任务依据**：[tasks-v2.md](../../tasks-v2.md) T8.2、[spec-v1.0-delta.md](../spec-v1.0-delta.md) O8/O9/DEC-3、[ADR-007](../adr/ADR-007-bilibili-online.md) 附条件接受。
- **交接**：[handoff-2026-10-23-t8.2.md](../handoff-2026-10-23-t8.2.md)。

## 0. 结论

1. **产品实际栈选 WinHTTP**（动态加载 `winhttp.dll`，不进导入表）。带普通用户 SESSDATA 的 `x/web-interface/nav` 自检：**HTTP 200、`code=0`、`isLogin=true`**，5 次样本 161–202ms；`dist\lapian.exe` 也通过。
2. **libcurl 未构建**（`stack=libcurl status=not_built`，退出码 2）。T8.2 不需要它；若将来要换栈，按 T8.16 处理 DLL/许可/体积。
3. **SESSDATA 只由 nav 请求显式发送到 `https://api.bilibili.com:443`**；客户端拒绝其它 host / 明文 HTTP / 非 443 / CR-LF 注入，禁用 cookie jar，不跟随重定向。
4. **DPAPI 当前用户**加密保存到 `%APPDATA%\拉片软件\credentials.dat`；磁盘无明文、损坏/截断按无凭据、清除后匿名。哨兵扫描 18 个日志文件 0 命中。
5. **DEC-3**：有凭据时只在用户首次提交合法 BV/URL 后自检一次；打开对话框、保存/清除凭据、非法输入、本地模式都不联网；失败继续匿名 view，界面明确“登录不可用/凭据失效；将按未登录清晰度播放”。

> 这台开发机在本会话的默认 DSH 文件沙箱下，Schannel/WinHTTP 无法写 `%APPDATA%\Microsoft\Crypto\RSA`，会报 `SEC_E_NO_CREDENTIALS`；自检在 `danger-full-access` 下运行。普通桌面进程不受此限制。这不是产品缺陷，也不是 TLS 校验关闭。

## 1. 环境与账号

| 项 | 值 |
|---|---|
| 机器 | Windows 11（10.0.26200），x64，桌面 `DESKTOP-DA6VQGS` |
| 构建 | Visual Studio 17 2022 / MSVC 19.44；Qt 6.8.3；FFmpeg 7.1.1 minimal |
| 网络 | 中国大陆网络，系统 WinHTTP 直连（`netsh winhttp show proxy`：Direct access，无代理）；未使用 `HTTPS_PROXY` |
| 账号 | **普通用户（非大会员）**；nav `vipStatus=0`；不记录/不打印账号标识 |
| 凭据来源 | `.secrets\bilibili_sessdata.txt`（单行；`.gitignore` 已忽略；未入库、未回显） |
| TLS | WinHTTP 默认证书校验；无 `-k`、无 `SECURITY_FLAG_IGNORE_*`、无 `CURLOPT_SSL_VERIFYPEER=0` |

## 2. 命令与栈

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"

# 产品实际栈（默认 auto：先 WinHTTP；通过即不试 libcurl）
build\release\bin\lapian.exe --online-http-selfcheck --http-stack=winhttp
build\release\bin\lapian.exe --online-http-selfcheck --http-stack=auto
build\release\bin\lapian.exe --online-http-selfcheck --http-stack=libcurl

# 打包后
dist\lapian.exe --online-http-selfcheck --http-stack=winhttp

# 失败/负向
$env:LAPIAN_SESSDATA_FILE = 'build\t8.2-selfcheck\invalid-sessdata.txt'
build\release\bin\lapian.exe --online-http-selfcheck --http-stack=winhttp
```

- 凭据文件：默认 `.secrets\bilibili_sessdata.txt`；或 `LAPIAN_SESSDATA_FILE` 指定**路径**。不支持命令行明文 secret；`--sessdata-file <path>` 只传路径。
- 输出字段：`stack/code/is_login/http_status/elapsed_ms/cookie_sent/vip_status/error`，失败时附 `message`（已脱敏）。
- 退出码：`0` = 至少一种栈通过；`1` = 都不通过；`2` = secret/参数/栈不可用。

## 3. 成功表（真实 nav，普通用户）

5 次连续样本（同一个 SESSDATA，同一时段；`build\release\bin\lapian.exe --http-stack=winhttp`）：

| # | stack | code | is_login | http_status | elapsed_ms | cookie_sent | vip_status | error | 退出 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | winhttp | 0 | 1 | 200 | 165 | 1 | 0 | none | 0 |
| 2 | winhttp | 0 | 1 | 200 | 185 | 1 | 0 | none | 0 |
| 3 | winhttp | 0 | 1 | 200 | 161 | 1 | 0 | none | 0 |
| 4 | winhttp | 0 | 1 | 200 | 202 | 1 | 0 | none | 0 |
| 5 | winhttp | 0 | 1 | 200 | 167 | 1 | 0 | none | 0 |

- **min 161ms / median 167ms / max 202ms**；全部 `code=0/isLogin=true`。
- `--http-stack=auto`：`stack=winhttp ... elapsed_ms=153 ... verdict=pass`，退出 0。
- `dist\lapian.exe --http-stack=winhttp`：`elapsed_ms=177`，`verdict=pass`，退出 0。
- 输出示例（不包含 Cookie 值）：

```text
T8.2 online_http_selfcheck stack=winhttp timeout_ms=2000 secret_source=default
stack=winhttp code=0 is_login=1 http_status=200 elapsed_ms=165 cookie_sent=1 vip_status=0 error=none message="已登录"
verdict=pass
```

## 4. 失败/负向表

| 场景 | 方法 | 结果 | 证据 |
|---|---|---|---|
| 凭据失效/未登录 | 用合法格式的假 SESSDATA 请求真实 nav | `code=-101 is_login=0 http_status=200 elapsed_ms=158 cookie_sent=1 error=api`；message=“登录不可用/凭据失效；将按未登录清晰度播放”；退出 1 | `build\t8.2-selfcheck\invalid.out.txt` |
| `-101` 解析 | fixture + fake HTTP | `login_unavailable`；message 明确“按未登录”；不假装登录 | `lapian_online_auth_test` |
| 超时 | fake HTTP `timed_out=true` | `AuthError::kTimeout`；message 含“超时”“未登录”；不拖住 view | `lapian_online_auth_test` / `--online-auth-test` |
| 取消 | fake 阻塞 nav，80ms 后取消 | `cancelled` ≤100ms；不缓存取消 | `lapian_online_auth_test` |
| HTTP 412/429 | fixture | 可读“被拒绝/风控限频；按未登录播放” | `lapian_online_auth_test` |
| 坏 JSON | fixture | `parse` 可读错误；按未登录 | `lapian_online_auth_test` |
| 非 API host 带 Cookie | `http://127.0.0.1:9` / `https://evil.example` | 本地拒绝，不发请求；错误含 `api.bilibili.com`，不含 sentinel | `lapian_online_auth_test` |
| 非 443 / 明文 HTTP | `https://api.bilibili.com:8443` / `http://api...` | 本地拒绝 | `lapian_online_auth_test` |
| CR/LF 注入 | `bad\r\nX-Evil: 1` | 本地拒绝；错误不含注入内容 | `lapian_online_auth_test` |
| 凭据文件损坏/截断/错误 magic | 篡改 `credentials.dat` | `corrupt=true`、`has_credential=false`、不崩 | `lapian_online_auth_test` |
| 清除凭据 | `Clear()` | 文件删除；后续按匿名；界面同步 | `lapian_online_auth_test` / `--online-auth-test` |

## 5. Cookie 去向、TLS 与代理

### 5.1 Cookie 只去 API

实现约束（`src/online/WinHttpClient.cpp`）：

- `HttpRequest::sessdata` 非空时，**只有** `parsed.secure && port==443 && host=="api.bilibili.com"` 才加 `Cookie: SESSDATA=<value>`；否则本地拒绝发送。
- `WINHTTP_DISABLE_COOKIES` 保留：不使用 WinHTTP cookie jar，不自动回传 `Set-Cookie`。
- `WINHTTP_OPTION_REDIRECT_POLICY_NEVER`：**不跟随任何重定向**；设置失败即拒绝发送，避免 30x 把 Cookie 带到别的 host。
- SESSDATA 值只允许单行可打印 ASCII，拒绝 `; , " \` 和 CR/LF；`SESSDATA=` 前缀会被剥离。
- 请求日志只记 `host/path/status/elapsed`；不打印 headers、Cookie、查询串、响应体。

证据：

- `--online-http-selfcheck` 输出 `cookie_sent=1`，且 B 站返回 `isLogin=true`，证明该请求确实带了 SESSDATA 并通过校验。
- `--online-auth-test` 断言：nav 请求 `cookie_sent=1`；**view 请求 `sessdata` 为空**；非 API host/明文/非 443/CRLF 均在本地拒绝。
- T8.2 **没有 CDN 请求**（未做 playurl/T8.5）；T8.5 取流时必须保持“CDN 不带 Cookie”，并在 T8.5 用抓包/代理再证。

### 5.2 TLS

- WinHTTP 默认校验证书；代码没有设置 `WINHTTP_OPTION_SECURITY_FLAGS`，没有 `SECURITY_FLAG_IGNORE_*`，没有 `-k` 或关闭校验的调试开关。
- TLS/证书失败映射为“TLS/证书校验失败，无法安全连接”（`ERROR_WINHTTP_SECURE_FAILURE`），不会降级重试。
- `check-imports.ps1` 通过：`lapian.exe` 不导入 `WINHTTP.DLL`/`WININET.DLL`/`URLMON.DLL`/`WSOCK32.DLL`；WinHTTP 仍是 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 动态加载，本地模式不初始化网络栈。
- **未做抓包**（没有可用的系统级抓包/代理工具）；本次以代码约束 + 本地 host 白名单单测 + `cookie_sent=1` + 真实 nav 成功作为证据。抓包留 T8.5 取流阶段补。

### 5.3 代理

- `netsh winhttp show proxy`：`Direct access (no proxy server)`；无 `HTTP(S)_PROXY` 环境变量。
- WinHTTP 使用 `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY`，即跟随用户系统代理；不额外改代理，不做地域伪装。

## 6. 凭据、DPAPI 与脱敏证据

| 验收 | 结果 | 证据 |
|---|---|---|
| O8-A1 磁盘无明文 | `credentials.dat` 含 magic + DPAPI 密文；sentinel 扫描 0 命中 | `lapian_online_auth_test` |
| DPAPI 往返 | 保存后 `Load()` 得到同一 SESSDATA；两个 `CredentialStore` 实例都可读 | 同上 |
| 损坏回退 | 随机文本/截断/错误 magic/目录占用 → 按无凭据，不崩 | 同上 |
| 清除后匿名 | `Clear()` 删除文件；`Has()=false`；`Load().missing=true` | 同上 |
| 原子写 | `QSaveFile`；保存后目录只有 `credentials.dat`，无临时文件 | 同上 |
| 只收 SESSDATA | 空/多行/`;`/`,`/引号/非 ASCII/超长拒绝 | 同上 |
| 哨兵日志扫描 | 179 个文件（selfcheck/auth-probe 输出、构建日志、`docs/*.md`、`AGENTS.md`、`tasks-v2.md`），真实 SESSDATA 0 命中 | `build\t8.2-selfcheck\scan-secret.ps1`（`hits=0`） |
| 不写 meta/index.jsonl/截图名 | T8.2 未实现缓存/在线记录；代码路径无 SESSDATA 落盘 | 代码审计；T8.3/T8.5 继续约束 |
| 隐私说明 | 在线对话框打开即显示；可“隐藏说明”；本地模式不显示 | `--online-auth-test` `privacy-visible/privacy-text/privacy-hidden` |

DPAPI 细节：`CryptProtectData`/`CryptUnprotectData`，`CRYPTPROTECT_UI_FORBIDDEN`；**不使用** `CRYPTPROTECT_LOCAL_MACHINE`；固定应用 entropy；文件格式 `LAPCRED1` + uint32 小端长度 + 密文。

## 7. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **24/24 通过**（17.52s；新增 `lapian_online_auth_test`） |
| T8.2 单测 | `lapian_online_auth_test` **104/0**（DPAPI/脱敏/nav/超时/取消/Cookie host） |
| `--online-auth-test` | **54/0**；nav 只发 1 次；第二次提交 0 nav；取消 0ms；view 不带 Cookie；打开/保存/清除全程未加载 `winhttp.dll` |
| `--online-resolve-test`（build） | **156/0**；`max_local_invalid_ms=0`；`confirmed_events=2` |
| `dist\lapian.exe --online-resolve-test` | **156/0** |
| 本地 P1/P2（proxy-V1） | `ui_ready=267.83ms`（≤1.5s）、`first_present=324.17ms`（≤2.5s） |
| 本地 P7（2s GOP，keyframe→PgUp，20 次） | p50 **60.55ms**、p95 **66.64ms**、max **66.84ms**、over 0、frame_failures 0、pass |
| 本地零网络审计 | 5s 本地内存探针 15 次采样：`winhttp_loaded=False`、TCP max 0、UDP max 0、HTTP 日志 0；`memory_summary pass=1` |
| dist 体积/导入 | 25 文件 / **32.89MB**（P12 ≤100MB）；`check-imports` 20 个 exe/dll 无 QtNetwork/TLS/winhttp/wininet/urlmon |
| 本地模式不显示隐私说明 | 在线对话框才显示；本地打开路径未创建对话框 |

## 8. 缺口 / 未验证

1. **libcurl 未构建**：`--http-stack=libcurl` 返回 `not_built`（退出 2）；T8.2 门只用 WinHTTP。若 T8.16 要换栈，需补 DLL/许可/体积与同样的 Cookie/TLS 约束。
2. **没有抓包/代理日志**：Cookie 只去 API 由客户端 host 白名单 + 单测 + `cookie_sent=1` 证明；T8.5 CDN 阶段必须补抓包，证明 CDN 不带 Cookie。
3. **没有对真实 B 站请求做取消**：真实 nav 161–202ms；取消用 fake 阻塞 + 本地慢服务器覆盖（≤100ms）。T8.5 下载阶段再补真实取消。
4. **DPAPI 只验当前用户**：没有跨 Windows 用户/漫游配置测试；没有内存取证（明文只在内存短暂存在，尽力清零）。
5. **未验证 412/429 的真实触发**：只做 fixture 映射；真实风控行为不可控。
6. **未做系统代理/TLS 中间人场景**：本机直连；系统代理照常生效，未专门测代理证书。
7. **T8.5/T8.16 仍要**：playurl/取流时 Cookie 只发 API、CDN 不带；打包时若引入 libcurl 的体积/许可；隐私文本最终版。
8. **账号类型**：本次只用普通用户（非大会员）验证 T8.2；大会员档仍按 T8.15 条件验收，不得写成 MVP 通过。

## 9. 复现

```powershell
# 1) 把单行 SESSDATA 放入 .secrets\bilibili_sessdata.txt（不要提交）
# 2) 构建
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 3) 门
build\release\bin\lapian.exe --online-http-selfcheck --http-stack=winhttp
build\release\bin\lapian.exe --online-auth-test
build\release\bin\lapian.exe --online-resolve-test

# 4) 打包与导入表
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -SkipTests
dist\lapian.exe --online-http-selfcheck --http-stack=winhttp
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1
```

> 本机注意：如果 DSH 文件沙箱是 workspace-write，Schannel 写 `%APPDATA%\Microsoft\Crypto` 会被拒绝并报 `SEC_E_NO_CREDENTIALS`；这是测试环境限制，需在允许写该目录的模式下运行自检。
