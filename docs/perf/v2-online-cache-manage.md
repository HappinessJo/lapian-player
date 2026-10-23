# T8.3.1 缓存管理证据（v2 在线）

- **版本**：v1.0（2026-10-24）
- **状态**：**T8.3.1 完成（非门）**；没有做 T8.6.1b、T8.9/M2、DASH、扫码/安装器。**M1/M2 门、验收数字、OB 样本未改**。
- **范围**：设置中自选缓存目录、修改配额、打开缓存目录、清空缓存、启动/定期孤儿清理；清空保留在线记录/截图；不误删当前会话；磁盘满/只读路径可读错误；默认路径只在 Medium 副本验收。
- **任务依据**：[tasks-v2.md](../../tasks-v2.md) T8.3.1、[spec-v1.0-delta.md](../spec-v1.0-delta.md) O5/O10/§4、[plan.md](../../plan.md) v2 章节、[ADR-007](../adr/ADR-007-bilibili-online.md)。
- **交接**：[handoff-2026-10-24-t8.3.1.md](../handoff-2026-10-24-t8.3.1.md)。
- **一句话**：缓存目录/配额/清空/孤儿清理已进产品设置对话框；改目录/配额跨会话；清空只删缓存布局内的数据，当前会话、在线记录、截图都不动；默认路径/真实 AppData 在 Medium 副本验证，工作区 Low 结果不冒充。

---

## 0. 结论

1. **设置入口**：状态栏“设置”按钮 + `Ctrl+,` 打开“设置 — 在线缓存”。可改缓存目录、配额（1–100GB）、保存；显示当前占用/配额/条目（完成/未完成/损坏）、磁盘可用空间、在线记录条数与目录。
2. **目录/配额跨会话**：目录与配额写入 `settings.json`（`cache_root`/`cache_quota_gb`，schema 仍 1）；新实例读取后应用。探针写入 `cache-b`/7GB 后新 store 读回 `7,516,192,768` 字节；`ApplyCacheSettingsForTest` 写 9GB 后新 `SettingsStore` 读到 9。
3. **改目录不迁移**：旧根数据原样保留；新根空；设置保存成功后才替换 `CacheStore`，保存失败不换对象、不删除新根内容。
4. **清空保留当前会话**：`ClearAll` 新增 `keep` 参数；`bvid+cid` 部分键（qn/codecid=0）保护当前视频全部档位。当前 `.part` 保留，其它完整/损坏/孤儿条目删除；空闲清空后 `usage=0`。
5. **在线记录/截图在缓存根外**：清空/孤儿清理后记录 JSON 逐字节不变、sentinel PNG 仍在；`lapian-online-1` 记录目录不参与 `ClearAll`。
6. **孤儿/过期清理**：启动 3s 后一次、之后每 6h；无 meta `.part/.mp4`、>24h 未完成、`evict_pending` 删除，当前会话 48h 也不删；清理后按配额 LRU 回收。探针一次清掉 2 个条目/5,431B，当前条目保留。
7. **只读路径**：ACL 拒绝写后 `SetRoot` 返回 **“缓存目录不可写（只读或权限不足）：…（拒绝访问。）”**，当前根不变；对话框显示红色“目录不可用”，在线打开显示同一可读错误。
8. **磁盘满**：2PB 不可满足的 expected 走同一预检，**0.116ms** 返回“磁盘空间不足：需要 1819.0 TB，可用 309.4 GB”；Medium C: 28.8GB 可用时 0.291ms。不开始下载、已有缓存仍可播。
9. **默认路径/Low 工作区**：默认缓存根 `%LOCALAPPDATA%\拉片软件\cache`、记录根 `%APPDATA%\拉片软件\online`；默认根不在工作区。Medium 副本 `integrity=medium` 下 **101/0**；Low 工作区只跑临时根，不把 `dist`/工作区当默认路径。
10. **真实用户数据未动**：默认根 18 个文件/246,972,833B、记录 1 条、`settings.json` 与备份逐字节一致；安装版 exe 已验证后恢复备份；测试用 AppData 子目录（带 pid）全部删除。
11. **回归**：`ctest` **31/31**（30.71s），新增 `lapian_online_cache_manage_test` **73/0**；`--online-cache-manage-test` workspace **72/0**（无 UI）/ **98/0**（`--ui`）；只读 ACL **75/0**；dist **33.87MB/25**（P12 余量 66.13MB）；本地 `--key-test` **77/0**（2s 27 帧）、P1 278.75–284.70ms、P2 333.63–343.29ms。

---

## 1. 环境与数据安全

| 项 | 值 |
|---|---|
| 机器 | Windows 11（10.0.26200），x64，`DESKTOP-DA6VQGS` |
| 构建 | Visual Studio 17 2022 / MSVC 19.44；Qt 6.8.3；CMake 3.31.6 |
| 工作区 | Low integrity（探针输出 `integrity=low`）；只用于临时根/无 GUI/只读脚本 |
| 默认路径副本 | `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`（Medium，`integrity=medium`）；验证后恢复原安装 exe |
| 默认缓存（只读摘要） | `%LOCALAPPDATA%\拉片软件\cache`：9 条、246,972,833B、18 个文件 |
| 默认在线记录（只读摘要） | `%APPDATA%\拉片软件\online`：1 条 |
| 网络 | 本任务不联网；无新网络导入 |
| 测试根 | `build\t8.3.1-*`；Medium 默认路径用 `%LOCALAPPDATA%\拉片软件\cache-t8.3.1-test-<pid>` 等唯一子目录，结束删除 |

**数据安全流程**：先把默认 `cache`、`online`、`settings.json` 复制到 `build\t8.3.1-backup-<ts>`（cache 18 文件/246,972,833B），记录原安装 exe；默认路径验收只**只读扫描**真实缓存根/记录根，破坏性用例全部在带 pid 的 AppData 测试子目录或 `build\t8.3.1-*` 临时根。结束后对比：缓存 18/18、`cache_diff=0`，settings hash 一致，记录 1 条 hash 一致；安装 exe 恢复为备份 hash。凭据 `credentials.dat` 未读取/未打印。

---

## 2. 命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 纯核心单测
build\release\Release\lapian_online_cache_manage_test.exe

# 无 GUI 探针（临时根；不碰真实 AppData）
build\release\bin\lapian.exe --online-cache-manage-test

# 设置对话框 + MainWindow 清理入口 smoke
build\release\bin\lapian.exe --online-cache-manage-test --ui

# 只读 ACL（build exe；先设置 PATH 或使用 Medium 安装版）
powershell -ExecutionPolicy Bypass -File scripts\cache-manage-readonly.ps1

# Medium 默认路径（必须先复制本次 build 到安装目录；结束恢复原 exe）
& "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe" `
  --online-cache-manage-test --manage-default-paths --ui
```

探针调试参数：`--manage-root`、`--manage-records-root`、`--manage-settings-root`、`--manage-readonly-root`、`--manage-default-paths`、`--manage-keep`。

---

## 3. 设置对话框与入口

- **入口**：状态栏永久控件“设置”（objectName `cacheSettingsButton`）+ 窗口 action `Ctrl+,`（`cacheSettingsAction`）；文本输入聚焦时不改既有键位表，v1 `§3.7` 不变。
- **缓存目录**：只读路径框 + “更改目录...”（`QFileDialog`）+ “恢复默认”；提示“不迁移旧缓存、旧目录文件保留”。保存后 `settings.json` 更新并立即应用到 `CacheStore`；根变化后旧根不迁移。
- **配额与占用**：`QSpinBox` 1–100GB；显示 `当前占用 X / 配额 Y（N 条：完成 C、未完成 I、损坏 B）`、`磁盘可用空间：X`。
- **在线记录**：显示 `%APPDATA%\拉片软件\online`（或测试根）条数/字节；文案明确“截图保存在图片目录；清空缓存不会删除记录和截图”。
- **操作**：打开缓存目录（`QDesktopServices`）、清理孤儿（`CleanupOptions{}`，不动完整条目）、清空缓存（确认框；Yes/No 默认 No）。
- **在线会话中**：目录/配额/保存禁用；清空仍可用，但保留当前 `bvid+cid` 条目，按钮文案“清空其他缓存...”。
- **启动/定期**：MainWindow 启动 3s 后 `RunCacheMaintenance("startup")`，之后每 6h `periodic`；清理 + 配额回收。测试模式（任何 `*-test`）由 main 关闭自动维护，避免在真实默认根上跑破坏性用例。

---

## 4. 目录/配额跨会话

| 项 | 结果 |
|---|---|
| 设置写入 | `cache_root=<temp>\cache-2`、`cache_quota_gb=7`，`SettingsStore::Save` 成功 |
| 新实例读取 | `status=kLoaded`、root 相同、quota=7 |
| 应用后 | `store.root()=cache-2`、`store.quota()=7,516,192,768` |
| 重开 | 新 `CacheStore` quota 相同、usage 相同 |
| 产品 MainWindow | `ApplyCacheSettingsForTest(root2,9)` 后新 `SettingsStore` 读到 quota=9、root=root2 |
| 改目录 | 旧根 `CountEntriesAtRoot>0`；新根 usage=0；旧缓存未迁移/未删除 |
| 保存失败保护 | 先写 settings，成功后才替换 `online_cache_`；失败时对话框持有的 store 仍有效 |
| 默认配额 | `DefaultQuotaBytes()=20GiB`；设置层 clamp 1–100GB |

配额调小后立即 `EvictToQuota`；读句柄占用时只提示“暂未完全回收”，不阻止设置保存。

---

## 5. 清空：当前会话 / 记录 / 截图

一次探针运行（`--online-cache-manage-test`）：

| 项 | 数值/结果 |
|---|---|
| 清空前占用 | **6,882,601B**（5 条；2 完整 + 1 损坏 + 1 未完成 + 1 无 meta 孤儿） |
| 当前会话保护键 | 未完成条目 D 的 `BV…/104/64-7`，传入时 qn/codecid=0 |
| 清空（保留当前） | `kept=1`、删除 **5,833,707B**；A/B/损坏 C/孤儿 E 全部删除 |
| 保留后占用 | **1,048,894B**（当前 `.part` + meta；不是 0，因为当前会话保留） |
| 在线记录 | 1 条、清空前后逐字节一致 |
| 截图 sentinel | `shot-sentinel.png` 清空后仍存在 |
| 再清空（无当前） | 删除 **1,048,894B**、`usage=0`、根目录/`readme.txt` 保留、记录/PNG 保留 |
| 幂等 | 第二次清空 `removed=0` |

- 只有缓存布局 `<root>\<bvid>\<cid>\<qn>-<codecid>\{meta.json,data.part,data.mp4}` 内的数据会被删；非布局文件/目录原样保留。
- 当前会话保护支持“完整键”和“bvid+cid 部分键”；后者覆盖 playurl 未返回 qn 的短暂窗口，避免删除刚 `Reserve` 的 `.part`。
- 记录/截图在缓存根外（T8.7），`ClearAll`/`CleanupOrphans` 不触碰。

---

## 6. 启动/定期孤儿清理

| 用例 | 结果 |
|---|---|
| 当前未完成 48h 未访问 | **保留**（当前会话永不清理；部分键生效） |
| 未完成 25h 未访问 | 删除 |
| 未完成 23h 未访问 | 保留（默认阈值 24h） |
| 完整条目 400 天未访问 | 保留（只清未完成/孤儿/pending） |
| 无 meta `.part`/`.mp4` 孤儿 | 删除 |
| `CleanupOptions.enabled=false` | 一个都不删 |
| 本次清理 | `removed_entries=2`、`removed_bytes=5,431`、`usage_after=1,051,580`、`current_protected=1` |
| 记录/截图 | 清理后仍存在，记录条数不变 |
| MainWindow | `RunCacheMaintenanceForTest()` 同一路径：`reason=test`、`removed_entries≥1`、孤儿目录消失、`CacheRootReady=1`；启动/每 6h 定时器调用同一函数 |
| 失败语义 | 单条删除失败/读句柄占用不视为整体失败，下一轮再试；清理后按配额 LRU 回收 |

`CleanupOrphans` 新增 `removed_entries` 计数；`EvictToQuota` 与 `ClearAll` 的部分键保护共用同一匹配规则。

---

## 7. 只读路径（ACL）

```text
[T8.3.1] deny write: DESKTOP-DA6VQGS\HappinessJo on ...\readonly
integrity=low
[ OK ] readonly-set-root-fails
[ OK ] readonly-error-readable
[ OK ] readonly-apply-fails
readonly_error=缓存目录不可写（只读或权限不足）：E:/拉片软件开发/build/t8.3.1-readonly/...（拒绝访问。）
readonly=pass
checks=75 failures=0 verdict=pass
```

- 拒绝写后 `SetRoot` 失败、当前 store 根不变；`ApplyRootAndQuota` 也失败，不会把只读目录写进 settings。
- 对话框 `Summary()` 显示红色“目录不可用：…”，保存时同一条错误；在线打开时报“在线缓存目录不可用：…”。
- 探针默认（无 ACL）用“文件冒充目录”覆盖非法路径错误：“缓存根不是目录”。

---

## 8. 磁盘满

| 环境 | 结果 |
|---|---|
| build（E: 309.4GB 可用） | `Reserve(2PB)` 失败 **0.116ms**；“磁盘空间不足：需要 1819.0 TB，可用 309.4 GB” |
| Medium（C: 28.8GB 可用） | 失败 **0.291ms**；同格式可读 |
| 不开始下载 | 不建条目目录/`.part`；已有缓存仍可 `OpenComplete` |
| 说明 | 这是预检路径，**没有真写满磁盘**；下载中途 `WriteFile` 失败属 T8.5/T8.13 |

---

## 9. 默认路径与 Low 工作区

Medium 副本（`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`）`--manage-default-paths --ui`：

```text
integrity=medium
default_cache_root=C:/Users/HappinessJo/AppData/Local/拉片软件/cache
default_records_root=C:/Users/HappinessJo/AppData/Roaming/拉片软件/online
default_cache_entries=9
default_cache_usage=246972833
default_records_count=1
default_paths=appdata-test-subdir
[OK] default-root-under-localappdata
[OK] default-records-under-appdata
[OK] default-root-not-workspace
[OK] default-records-not-workspace
...
checks=101 failures=0 verdict=pass
```

- 默认根断言：`DefaultRoot()=%LOCALAPPDATA%\拉片软件\cache`；`DefaultRootFor("C:/Users/x/AppData/Local")` 不叠第二层；默认记录根 `%APPDATA%\拉片软件\online`；两者都不是工作区/临时根。
- Medium 的**破坏性用例**落在 `%LOCALAPPDATA%\拉片软件\cache-t8.3.1-test-<pid>`、`%APPDATA%\拉片软件\online-t8.3.1-test-<pid>`、`settings-…-<pid>`；真实默认根只读摘要，不跑清空/孤儿清理。结束后测试子目录全部删除。
- 工作区 Low 运行（build/dist）只跑临时根；**不把 `build`/`dist` 当默认路径**，也不把 Low 的写失败当产品默认路径失败。Default-path 探针检测到非 Medium 会直接 `exit 2`。
- 真实默认根 18 文件/246,972,833B、记录 1 条、settings 与备份逐字节一致。

---

## 10. UI 与 MainWindow 集成

| 检查 | 结果 |
|---|---|
| 对话框 root/quota/usage/records 文案 | 通过（`ui-root-text`、`ui-quota-7`、`ui-usage-visible`、`ui-records-visible`） |
| 保存/清空按钮可用 | 通过 |
| 在线会话中 | 保存/配额禁用、清空仍可用（保留当前） |
| 清空 | 状态“已清空缓存…”、usage=0、记录/sentinel 保留 |
| 清理孤儿 | 状态“已清理孤儿/过期缓存…” |
| 打开目录 | 回调路径成功（自动化不启动 Explorer；产品走 `QDesktopServices`） |
| 保存 | `ApplyForTest` 成功，quota/root 生效 |
| MainWindow 入口 | `cacheSettingsButton`/`cacheSettingsAction` 存在 |
| MainWindow 设置持久化 | `ApplyCacheSettingsForTest(root2,9)` 后新 `SettingsStore` 读到 9/root2 |
| MainWindow 维护 | `RunCacheMaintenanceForTest` 删除孤儿，`reason=test`，根 ready |

工作区 `--ui` **98/0**；Medium 默认 AppData `--ui` **101/0**（多 3 项默认路径检查）。

---

## 11. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **31/31**（30.71s；新增 `lapian_online_cache_manage_test`） |
| `lapian_online_cache_manage_test` | **73/0**（默认根/记录根、跨会话、清空保护、孤儿、磁盘满/非法路径） |
| `lapian_online_cache_test`（T8.3） | 366/0（未退化） |
| `--online-cache-manage-test`（build，无 UI） | **72/0**，`verdict=pass`，149–172ms |
| `--online-cache-manage-test --ui`（build） | **98/0**，`verdict=pass` |
| `--online-cache-manage-test`（dist） | **72/0**，`verdict=pass` |
| `--online-cache-manage-test --ui`（dist） | **98/0**，`verdict=pass` |
| Medium 默认路径 `--ui` | **101/0**，`integrity=medium` |
| 只读 ACL（build） | **75/0**，`readonly=pass` |
| `dist` | **33.87MB / 25 文件**（P12 余量 66.13MB；T8.6.1a 为 33.69MB） |
| `check-imports` | 20 个 exe/dll 通过；无 QtNetwork/TLS/winhttp/wininet/urlmon |
| 本地 `--key-test` | **77/0**；2s **27 帧** |
| 本地 P1（3 次，proxy-V1） | ui_ready **283.72 / 278.75 / 284.70ms**（均 ≤1.5s） |
| 本地 P2（3 次，proxy-V1） | first_present **341.83 / 333.63 / 343.29ms**（均 ≤2.5s） |
| 本地打开/步进 | 通过；无在线记录/缓存写入（测试根） |

以上 P1/P2 是测试模式（自动维护关闭）的本地回归，不是在线 OP1/OP5；未改 M1/M2 口径。

---

## 12. 缺口 / 未验证

1. **启动 3s / 6h 定时器未真实等待**：用同一 `RunCacheMaintenance("test")` 路径验证；未跑 6h/长时定时漂移。
2. **Explorer 打开未自动化**：只验证路径存在与回调；真实资源管理器窗口需手测。
3. **默认根破坏性清理未跑**：按“不误删用户真实缓存”约束，真实默认根只读摘要；产品首次启动会在默认根清理 >24h 未完成条目——这是 T8.3 `CleanupOptions` 的既有语义，未在本机真实根执行。
4. **只读 ACL 在 build/Low 上跑**，不是 Medium 默认安装目录；同一 Win32/Qt 写探测路径，未在 Medium 复跑 ACL。
5. **磁盘满未真写满**：2PB 预检；下载中 `WriteFile`/`ERROR_DISK_FULL` 映射仍属 T8.5/T8.13。
6. **并发第二实例读句柄**：沿用 T8.3 的 busy/`evict_pending` 语义，未做双进程真实清空压测。
7. **大缓存扫描在 UI 线程**：3s 启动清理只在本机小缓存（9 条）验证；数千条目下的扫描耗时未测，可能需要 worker。
8. **对话框是独立设置页**，不是完整设置框架；没有主菜单，入口是状态栏按钮/Ctrl+,。
9. **截图 sentinel 是临时目录中的 PNG**，不是真实 `Pictures\拉片软件`；真实截图目录由 T8.7 决定，`ClearAll` 不遍历缓存根外。
10. **T8.6.1b/T8.9/M2 未做**；本任务不把缓存管理写成 M2 门通过。

---

## 13. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
build\release\bin\lapian.exe --online-cache-manage-test
build\release\bin\lapian.exe --online-cache-manage-test --ui
powershell -ExecutionPolicy Bypass -File scripts\cache-manage-readonly.ps1
```
