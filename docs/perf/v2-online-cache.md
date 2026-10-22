# T8.3 缓存存储与配额证据（v2 在线）

- **版本**：v1.0（2026-10-23）
- **状态**：**T8.3 完成（T8.3 范围内）**；没有 T8.4 读层、T8.5 取流、T8.6/T8.7 UI/记录。
- **范围**：缓存根/目录结构、`meta.json`、`.part`→`.mp4` 原子改名、配额与 LRU、占用/清空、空间预检、续传长度、损坏检测/重建、孤儿清理、路径/配额跨会话、Unicode/长路径。
- **任务依据**：[tasks-v2.md](../../tasks-v2.md) T8.3、[spec-v1.0-delta.md](../spec-v1.0-delta.md) §2.5 O5 / §4 / §9.0 DEC-5、[plan.md](../../plan.md) §12.1/§12.2/§12.6、[ADR-007](../adr/ADR-007-bilibili-online.md)。
- **交接**：[handoff-2026-10-23-t8.3.md](../handoff-2026-10-23-t8.3.md)。
- **一句话**：本地缓存现在有可验证的目录、meta、配额、LRU、续传与损坏/清空语义；**不联网、不发 playurl、不下载 CDN、不做可增长读层**。

## 0. 结论

1. **目录/字段按 delta §4.1**：`<根>\<bvid>\<cid>\<qn>-<codecid>\{meta.json,data.part,data.mp4}`；默认根 `%LOCALAPPDATA%\拉片软件\cache`（不叠第二层“拉片软件”），默认配额 **20 GiB**。
2. **写入原子性**：`meta.json` 用 `QSaveFile`（`setDirectWriteFallback(false)`）；`.part`→`.mp4` 用 `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`，路径统一加 `\\?\` 长路径前缀。失败保留 `.part` 并返回可读错误。
3. **300MB 配额 LRU**：3×90MB（最旧/中/新）+ 新条目 60MB，占用 283,116,579 > 配额 314,572,800 不足 → **只淘汰最旧 90MB**，占用降到 188,744,706 ≤ 配额；当前条目 `protected=1`。
4. **20GB 记账单测**（稀疏文件，**不真填满**，DEC-5 A）：配额 21,474,836,480；21 GiB+meta 超配额 → 淘汰 5 GiB → 16 GiB；再加 10 GiB → 淘汰 2 GiB + 14 GiB → 最终 **10,737,418,599 ≤ 20 GiB**。
5. **磁盘满预检**：不可满足的 expected（2 PB）走与真实下载相同的预检路径，**0.082–0.115ms** 返回“磁盘空间不足：需要 X，可用 Y”；不建条目目录；已缓存条目仍可 `OpenComplete`。
6. **清空/孤儿**：`ClearAll` 只删缓存根下的条目数据（本次 99,615,708 字节），缓存根外的“在线记录/截图” sentinel 保留；孤儿 `.part/.mp4`、>24h 未访问的未完成条目、`evict_pending` 可清理，当前会话 key 永不清理。
7. **续传**：`.part` 实际长度为准（5,242,880 字节），meta 被改坏也按文件长度校正并回写；不产生双份；`.part` > total 判损坏。
8. **损坏/重建**：非法 JSON、大小不符、缺 `data.mp4`、缺 `meta.json`、key 不一致、未知 schema、meta 过大、崩溃在改名后 → 8 类全部标 broken（不可播放），`Rebuild` 只删该条目；崩溃在“改名成功但 meta 未落盘”时自动修正为完成。
9. **跨会话**：`settings.json` 新增 `cache_root`/`cache_quota_gb`（v1 键语义不变）；新进程仍生效；**改根不迁移旧缓存**（旧根文件保留，新根为空）。
10. **长路径/Unicode**：301–312 字符中文条目路径的创建、改名、读取、重建、清空通过；meta 扫描 **0** 处 Cookie/URL 明文。
11. **不退化**：没有新增任何网络导入（`check-imports` 20 个文件通过）；`ctest` **25/25**；`--online-resolve-test` 156/0；`--online-auth-test` 54/0；本地 P1/P2/P7 无回归（见 §13）。

## 1. 环境与范围

| 项 | 值 |
|---|---|
| 机器 | Windows 11（10.0.26200），x64，桌面 `DESKTOP-DA6VQGS` |
| 构建 | Visual Studio 17 2022 / MSVC 19.44；Qt 6.8.3；CMake 3.31.6（`third_party\cmake`） |
| 磁盘 | E: 可用约 310GB（测试用稀疏文件，逻辑大小 270MB/21GiB，不真占盘） |
| 网络 | **不需要**；T8.3 探针不加载 `winhttp.dll`、不发任何请求、不需要 SESSDATA |
| 缓存根 | 测试用 `build\release\t8.3-cache-<pid>`（退出即删）；默认根只读不写 |
| 不含 | T8.4 `GrowingSource`/Cursor/AVIO/多读者、T8.5 playurl/CDN/Range GET、T8.6/T8.7 UI/缓存命中/在线记录、DASH、4K/1080P60 |

## 2. 命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 纯存储单测（ctest 里也跑）
build\release\Release\lapian_online_cache_test.exe

# 无 GUI 探针（临时根、合成文件、不联网、不需要 SESSDATA）
build\release\bin\lapian.exe --online-cache-test
# 调试时保留临时根：
build\release\bin\lapian.exe --online-cache-test --cache-test-keep --cache-test-root build\t8.3-cache-debug

# 打包后
dist\lapian.exe --online-cache-test
```

- 探针输出（key=value）：`default_root/root/quota_bytes/usage_before/usage_after/entries_before/lru_before/lru_after/evicted_keys/evicted/protected/resume_bytes/corrupt/rebuild/clear/records_kept/space_error_ms/cross_session/long_path_len/long_path/plaintext/checks/failures/elapsed_ms/verdict`。
- 退出码：`0` = `verdict=pass`；`1` = 有失败；`2` = 临时根不可用。
- 探针每次运行会删掉自己的临时根与 sentinel 目录；不写真实 `%LOCALAPPDATA%`/`%APPDATA%`。

## 3. 目录、meta 与生命周期

```text
<缓存根>\
  <bvid>\                # 只接受 BV + 10 位 [0-9A-Za-z]
    <cid>\               # 正整数
      <qn>-<codecid>\    # 正整数；qn/codecid 来自 playurl，不猜
        meta.json        # QSaveFile 原子写；schema=lapian-cache-1
        data.part        # 下载中；连续前缀
        data.mp4         # 完成后 MoveFileExW 原子改名
```

`meta.json` 字段（缺省/非法时的处理）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `schema` | string | 必须是 `lapian-cache-1`；否则 broken（未来版本不静默读） |
| `bvid`/`cid`/`qn`/`codecid` | string/number | 必须与目录键一致；不一致 broken（防串片/复制错） |
| `title`/`owner`/`source_fps_hint` | string | 可缺省（空） |
| `duration_ms`/`width`/`height`/`fps` | number | 可缺省（0）；非法值回退 0 |
| `total_bytes` | number | 必须 >0 且为整数；缺失/非法 broken |
| `downloaded_bytes` | number | 信息字段；打开时按 `data.part` 实际长度校正 |
| `complete` | bool | 缺失 broken；`true` 要求 `data.mp4` 存在且大小 == total |
| `created_at`/`last_access_at` | ISO-8601（UTC） | 缺失时用文件 mtime；LRU 只看 `last_access_at` |
| `evict_pending` | bool（可选） | 淘汰时文件仍被读取：标记待淘汰，下次清理再删 |

- **不写**：SESSDATA、Cookie、请求头、带签名播放 URL（有单测扫描 meta）。
- 写入顺序：`Reserve` 建目录 → 建空 `.part` → 写 meta；`MarkComplete` 由调用方先关闭写句柄 → 校验长度 → 改名 → 写 `complete=true`。
- 进度写盘按 1s 节流（`kProgressWriteIntervalMs`），`force=true` 立即写；数值单调不回退。
- 键校验在拼路径**之前**：`../evil`、`bv...`、11/13 位、`/`、`\`、`cid/qn/codecid=0` 全部拒绝；非法键不建任何目录（单测断言）。

## 4. 300MB 配额与 LRU（DEC-5 A）

探针（`--online-cache-test`，真实一次运行）：

| # | 条目 | 大小 | last_access | 结果 |
|---|---|---|---|---|
| 1 | `…aa1/101/64-7` | 90MB | -4h（最旧） | **被淘汰** |
| 2 | `…bb2/102/64-7` | 90MB | -3h | 保留 |
| 3 | `…cc3/103/80-12` | 90MB | -2h | 保留（随后被截断做损坏用例） |
| 4 | `…dd4/104/64-7`（current） | 60MB 预留 | now | 保留，`protected=1` |

- 配额 `314,572,800`；预留前占用 `283,116,589`（270MB 数据 + 3×meta），+60MB 超出 → 从最旧开始淘汰。
- 淘汰后占用 `188,744,706` ≤ 配额；`evicted=1`、`evicted_keys=aa1/101/64-7`。
- `lru_before=aa1/101/64-7>bb2/102/64-7>cc3/103/80-12`，`lru_after=bb2/102/64-7>cc3/103/80-12>dd4/104/64-7`（按 `last_access_at` 升序）。
- 单测（`lapian_online_cache_test`）另有 5 段 LRU 断言：只淘汰最旧、三段连续淘汰顺序、`EvictToQuota` 幂等、当前 key 在“只有当前条目且超额”时不被删除但返回可读错误、配额 1MB 时改 current 后旧当前可淘汰。
- 记账口径：**`data.mp4` + `data.part` + `meta.json` 的逻辑文件大小**，不含在线记录/截图；测试数据文件用 `FSCTL_SET_SPARSE` 造逻辑大小，**不真写 300MB**。
- 淘汰前用 `CreateFileW(DELETE, share=0)` 独占打开判断读句柄；Qt/FFmpeg 真实读句柄的行为留给 T8.4/T8.5 验证（见 §14）。

## 5. 20GB 记账单测（不真填满）

| 步骤 | 条目 | 占用 | 结果 |
|---|---|---|---|
| 初始 | 5GiB + 2GiB + 14GiB（+3×meta） | `21×2^30 + meta` = 21 GiB 级 | 超过 20 GiB 配额 |
| 淘汰 | 最旧 5GiB | 16 GiB 级 | `acct-eviction-debits-exactly`：占用恰好减去该条 `data+meta` |
| 再加 | 10GiB（current） | 26 GiB 级 | 淘汰 2GiB（24）→ 淘汰 14GiB（10） |
| 最终 | 10GiB + meta | **`10,737,418,599`** ≤ `21,474,836,480` | 只保留 current |

- 数字来自单测输出：`quota_20gb=21474836480 usage_after=10737418599`。
- 文件为稀疏（10–14GiB 逻辑大小，物理占用近 0）；**没有**做“真 20GB 填满”端到端（DEC-5 明确列为可选长测 S5，不作为 M1/M2 门）。

## 6. 续传

| 用例 | 结果 |
|---|---|
| `.part` 5MB → 新 `CacheStore` 实例 `OpenPart` | `resume_bytes=5242880`；文件长度仍 5MB（不拼接双份） |
| meta `downloaded_bytes` 被改成 1，`Load` | 按文件长度校正为 5MB 并回写 meta（`resume-meta-rewritten`） |
| 续传时 `Reserve` 同 total | 不截断 `.part`（`resume-reserve-keeps-part`），再扩到 8MB 可继续 |
| `.part` 21MB > total 20MB | broken（“缓存前缀超过总字节”），不可播放 |
| total 变化（10MB → 20MB） | `Reserve` 拒绝并要求重建（“总字节与本次不一致”），不静默拼接 |
| 杀进程 | 单测用“新实例 + 同根”模拟；**没有**做真实 kill/断电测试（见 §14） |

## 7. 损坏检测与重建

| 用例 | 检测 | 结果 |
|---|---|---|
| meta 非法 JSON | broken “meta.json 不是合法 JSON” | `OpenComplete` 失败；`Rebuild` 删除条目 |
| `complete=true` 但 `data.mp4` 截断 | broken “缓存大小不符” | 不可播放 |
| `complete=true` 但缺 `data.mp4` | broken “complete=true 但缺少 data.mp4” | 不可播放 |
| meta 与目录键不一致 | broken “meta.json 与目录键不一致” | 不可播放 |
| 未知 schema | broken “不支持的缓存 schema” | 不可播放（不猜未来字段） |
| 缺 `meta.json`（只有 `data.mp4`） | broken “缺少 meta.json（只有数据文件）” | 孤儿；`Rebuild`/清理可删 |
| meta > 64 KiB | broken “meta.json 过大” | 不读进内存 |
| 改名成功、meta 未落盘 | **自动修正**为 `complete=true` 并回写 | 可播放（不误判损坏） |
| 所有用例的 `Rebuild` | 只删该条目 `data.part/data.mp4/meta.json`（含 `meta.json.*` 临时残留） | 同根其它条目、缓存根外记录/截图 sentinel 均保留 |

- 扫描不因单条损坏中断：`Scan(entries, broken)` 分别返回可播放与损坏条目；非本工具布局（`notabvid`、非数字 cid、`64-7-8`）忽略且不删除。

## 8. 清空、孤儿清理与读句柄

| 用例 | 结果 |
|---|---|
| `ClearAll`（4 条目：3 完整 + 1 未完成 + 1 损坏） | 删除 **99,615,708** 字节；`usage=0`；缓存根仍存在；第二次清空 `removed=0` |
| 缓存根外 sentinel（在线记录 JSON / 截图 PNG） | `records_kept=1`；内容逐字节不变 |
| 非布局文件 `readme.txt` 与垃圾目录 | 保留 |
| 孤儿 `data.part` / `data.mp4`（无 meta） | `CleanupOrphans` 删除（当前会话 key 除外） |
| 未完成条目 25h 未访问 | 删除；23h 保留；48h 的**当前**条目保留 |
| 完整条目 400 天未访问 | 保留（只清未完成） |
| `CleanupOptions.enabled=false` | 一个都不删 |
| `incomplete_max_age_hours=0` | 清掉 23h 的未完成；当前条目仍保留 |
| 淘汰时文件被读句柄占用 | `EvictToQuota` 返回失败并说明“正被读取”，meta 标 `evict_pending=true`；关句柄后 `CleanupOrphans` 删除；占用回到配额内 |

## 9. 磁盘空间预检（≤5s）

- `Reserve(expected=2,000,000,000,000,000)`（2 PB，任何测试机都不可能满足）走同一 `GetDiskFreeSpaceExW` 预检：**0.082ms（单测）/ 0.106–0.115ms（探针）** 返回：
  `磁盘空间不足：需要 1.8 TB，可用 289.5 GB`（格式为“需要 X，可用 Y”）。
- 失败后：不建条目目录、不建 `.part`；已缓存条目 `OpenComplete` 仍成功（“已缓存仍可播”）。
- **这是预检路径，不是真写满磁盘**；下载中途 `WriteFile` 失败的映射属于 T8.5。

## 10. 路径与配额跨会话

- `settings.json` 新键：`cache_root`（空 = 默认根）、`cache_quota_gb`（默认 20，clamp 1–100）；`schema` 仍是 1，v1 的 `volume`/`waveform` 语义不变；旧文件缺键 → 默认；未知键忽略。
- 跨会话：写 `cache_root=<临时>/cache-a`、`cache_quota_gb=7` → 新 `SettingsStore` 读回 → 新 `CacheStore` 用该根/配额；再开实例占用一致（`cross_session=pass`）。
- `SetRoot` 到新根：旧根文件保留、新根 `usage=0`（不迁移）；`SetRoot("")`/文件路径/不可写目录返回可读错误且不改当前根（含一次可写探测文件，删除后不残留）。
- 产品保存路径：`MainWindow::FlushSettings` 现在用 `MergeVolumeWaveform(loaded_settings_, …)`，只覆盖音量/波形，**不会把 `cache_root/cache_quota_gb` 冲回默认**（`lapian_app_settings_test` 覆盖纯函数）。

## 11. 长路径与 Unicode

- 单测：根 280 字符 + 条目目录 **301** 字符（中文目录名）；探针一次运行 **312** 字符。
- 覆盖：`Reserve` 建目录、写 `.part`、`MarkComplete`（`MoveFileExW` + `\\?\`）、`Load`/`OpenComplete`、`Rebuild`（含空父目录回收）。
- 若环境无法创建长路径根，单测打印 `[SKIP] long-path：<原因>` 并继续（本次 0 skip）。
- 未覆盖：>260 的 UNC 网络路径、`\\?\` 卷 GUID、文件系统压缩/加密目录（见 §14）。

## 12. 无明文与本地零网络

- 单测/探针只扫描缓存根下的 `*.json`：`SESSDATA`/`Cookie`/`http://`/`https://`/`playurl`/`access_key`/`token` **0 命中**（`plaintext=0`）。
- 缓存 meta 不含 URL/请求头，因此“URL 过期刷新”不需要改 meta（T8.5 重新解析 playurl）。
- T8.3 未新增任何网络依赖：`lapian_online` 仍只有动态加载的 WinHTTP；`scripts\check-imports.ps1` 20 个文件通过（仅 `Qt6Core.dll -> WS2_32` 已知项）。
- 本地模式启动/打开/步进不提速也不联网：探针无 GUI、在 `QApplication` 之前分流；不带 `--online-cache-test` 时不会构造 `CacheStore`。

## 13. 回归（同一提交）

| 项 | 结果 |
|---|---|
| `ctest` | **25/25 通过**（18.2s；新增 `lapian_online_cache_test`） |
| `lapian_online_cache_test` | **366 项 / 0 失败 / 0 skip**（758ms） |
| `lapian_app_settings_test` | **80 项 / 0 失败**（原 62 项 + 缓存键 18 项） |
| `--online-cache-test`（build） | **32 项 / 0 失败**，`verdict=pass`，`elapsed_ms=95.8` |
| `--online-cache-test`（dist） | `verdict=pass`，退出 0 |
| `--online-resolve-test` | **156 / 0**（build 与 dist） |
| `--online-auth-test` | **54 / 0** |
| `--settings-test --file proxy-V1` | 全部 `ok=1`，退出 0 |
| 本地 P1/P2（proxy-V1） | `ui_ready=290.73ms`、`first_present=345.13ms` |
| 本地 P7（2s GOP，20 次） | p50 56.56ms / p95 64.96ms / max 66.26ms / over 0 / `pass=1` |
| `dist` | **33.02MB / 25 文件**（P12 余量 66.98MB；T8.2 为 32.89MB） |
| `scripts\check-imports.ps1` | 20 个 exe/dll：无 QtNetwork/TLS/winhttp/wininet/urlmon/WSOCK32 |

“起播 5s”“已缓存 P7 80ms”“缓存命中 2.5s”“未缓存 100ms”“4K/1080P60”均**未在本任务测**，不写通过；上面的 P1/P2/P7 是本地模式回归基线，不是在线验收。

## 14. 缺口 / 未验证

1. **不是真 300MB/20GB 填满**：数据文件用稀疏/`SetEndOfFile` 造逻辑大小；`UsageBytes` 用文件逻辑大小（不是磁盘簇）。真实下载产生的大文件、真实磁盘碎片/簇占用未测。
2. **磁盘满只用不可满足的 expected 走预检**；没有真写满磁盘、也没有覆盖下载中 `WriteFile` 失败/`ERROR_DISK_FULL`（属 T8.5）。
3. **没有真实 playurl 数据**：qn/codecid/总字节/宽高/fps 是合成值；T8.5 接入后需要重跑同组用例（尤其 `total_bytes` 与 `Range` 一致性）。
4. **杀进程续传是“新实例 + 文件长度”模拟**，没有真实 `TerminateProcess`/断电；T8.5 的 Range 续传还要验证服务端 200 全量时“从头覆盖、不拼接”。
5. **读句柄检测**用 Win32 `CreateFileW(DELETE, share=0)`；Qt/FFmpeg 真实读句柄（是否带 `FILE_SHARE_DELETE`）留给 T8.4/T8.7 实测；若读句柄带 DELETE 共享，删除会成功但数据在句柄关闭前仍占空间（当前按“逻辑大小”记账）。
6. **长路径只验本机 NTFS + 中文目录**（301/312 字符）；UNC、`\\?\Volume{}`、OneDrive/压缩/加密目录未验。
7. **在线记录是 sentinel 目录**，不是真实 `%APPDATA%\拉片软件\online`（T8.7 才产生）；清空保留记录的语义已覆盖，路径细节留给 T8.7。
8. **没有 UI**：缓存根/配额只能改 `settings.json`（或后续 UI）；T8.6 才接入口；占用可见/单条占用也只有 API（`Scan`/`UsageBytes`）。
9. **20GB 真填满**（DEC-5 可选长测 S5）未做。
10. **杀软/文件锁**：只用人工独占句柄模拟，没有真实 AV 扫描占用；`evict_pending` 的真实触发频率未知。
11. **没有性能承诺**：本任务没有“缓存扫描 p95”“清空耗时”等指标要求；366 项单测 758ms、探针 95.8ms 只作回归参考。

## 15. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
build\release\bin\lapian.exe --online-cache-test
```

调试时加 `--cache-test-keep`，临时根在 `build\release\t8.3-cache-<pid>`（含 `cache/` 与 sentinel 目录）。
