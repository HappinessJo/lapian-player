# 下一个 agent 提示词：v2 在线 T8.3（缓存存储/配额/续传）

> 直接把下面整段复制给新会话。日期：2026-10-23。工作目录：`E:\拉片软件开发`。
> T8.2 已完成并提交：`0f322b0`（v2 S3 + T8.0–T8.2；工作树 clean）。

---

你是拉片软件 v2 在线模式的实现 agent。**本次只做 T8.3：缓存存储与配额（CacheStore）**。不做 T8.4 可增长读层、T8.5 `playurl`/下载/播放、T8.6 在线会话 UI、T8.7 缓存命中/在线记录；不做 DASH、4K/60fps 验收，也不做 UI 大改。

## 先读（按顺序）

1. `AGENTS.md` §1 v2 条目、§11 协作规则、§12 文档表。
2. `docs/spec-versioning.md` §1：v1 冻结 / v2 增量边界。
3. `docs/spec-v1.0-delta.md`：§2.5 O5 缓存与配额、§2.7 O7 错误/断网、§2.9 O9 本地零网络、§4 缓存与配额规范（目录/meta/生命周期/LRU/续传/缓存命中）、§5 失败矩阵、§9.0 DEC-5（300MB 自动 + 20GB 记账单测）。
4. `plan.md`：§12.1 组件边界（`CacheStore`）、§12.2 线程/并发、§12.3 数据流、§12.4 可增长读层（只看接口，不做）、§12.9 隐私/本地零网络；只读，不改 v1 正文。
5. `tasks-v2.md`：**T8.3 行** + “任务完成定义” + “审核门”；确认 T8.4 只依赖 T8.3。
6. `docs/perf/v2-online-spike.md`：§3.1 Range/首字节、§3.2 吞吐、§4 可增长读层（读层是 T8.4，不是 T8.3）。
7. `docs/handoff-2026-10-23-t8.2.md` 与 `docs/perf/v2-online-http-selfcheck.md`：T8.2 的接口/数字/缺口。
8. `src/core/AppSettings.h/.cpp` 与 `src/core/MarkStore.cpp`：`SettingsStore` 的 `%APPDATA%`/`QSaveFile` 模式、`MarkStore` 的原子写/长路径/损坏回退模式，可作为 T8.3 的参考，但**不要改 v1 sidecar schema**。
9. `scripts/check-imports.ps1`：本地模式导入表白名单；T8.3 不得引入网络导入。

## 当前已确定的事实（不要重做）

- **T8.2 已完成并提交 `0f322b0`**：WinHTTP nav 自检 `code=0/isLogin=true`（普通用户，5 次 161–202ms）；`HttpRequest::sessdata` 只发 `api.bilibili.com:443`；`CredentialStore` DPAPI；`BiliAuthSession`；`--online-http-selfcheck`/`--online-auth-test`；`ctest` 24/24、`--online-resolve-test` 156/0、`--online-auth-test` 54/0、dist 32.89MB/25 文件。
- **T8.1 已完成**：`BiliResolver`/`BiliResolveClient`/`OnlineResolveDialog`；BV/URL→匿名 `view`→标题/UP/时长/分P→回显确认；非法输入 0 请求；`--online-resolve-test` 156/0。
- **没有 T8.3 代码**：没有 `CacheStore`、没有 `meta.json`、没有 `.part`/`.mp4`、没有配额/LRU/续传；`%LOCALAPPDATA%\拉片软件\cache` 尚未使用。
- 本地模式运行期零网络；`check-imports.ps1` 禁止 `WINHTTP.DLL`/QtNetwork/TLS 导入；当前 WinHTTP 是动态加载。
- 已拍板：M1→M2；起播 p95 ≤5s / 已缓存 P7 ≤80ms / 缓存命中 ≤2.5s / 未缓存提示取消 ≤100ms / 任务完成率 ≥80%；DEC-5 = **300MB 自动 LRU + 20GB 记账单测 + 可选真填满**。4K/1080P60 只进 T8.15，不进 T8.3/M1/M2 门。

## T8.3 目标（DoD）

1. **缓存根/目录**：默认 `%LOCALAPPDATA%\拉片软件\cache`（用 `LOCALAPPDATA` 环境变量，避免 `QStandardPaths` 再拼一层 `拉片软件`）；可配置；结构：
   ```text
   <cache-root>\
     <bvid>\
       <cid>\
         <qn>-<codecid>\
           meta.json
           data.part     # 下载中；连续前缀
           data.mp4      # 完成后原子改名
   ```
   `qn/codecid` 来自 playurl，不猜；只允许合法 bvid/cid/qn/codecid，防路径穿越。
2. **`meta.json`**：至少 `schema`、`bvid`、`cid`、`title`、`owner`、`duration_ms`、`qn`、`codecid`、`width`、`height`、`fps`、`total_bytes`、`downloaded_bytes`、`complete`、`created_at`、`last_access_at`、`source_fps_hint`（可空）。**不存 SESSDATA、Cookie、请求头、带签名的播放 URL**。原子写；损坏/缺字段不崩。
3. **配额**：默认 **20GB**，可配置（建议 1–100GB，步进 1GB）；计算口径 = `data.mp4` + `data.part` + `meta.json` 实际字节；**LRU 按 `last_access_at`**；**当前会话 key 永不淘汰**；淘汰后总占用 ≤ 配额；淘汰前确认无读句柄，或标记待淘汰。
4. **空间预检**：写入前 `可用空间 ≥ 预计总字节 + 512MB`；不足返回 ≤5s 可读错误（“磁盘空间不足：需要 X，可用 Y”），不开始下载；已缓存仍可播。
5. **原子改名/续传**：下载写 `data.part`；完成关闭写句柄 → 原子改名 `data.mp4` → meta `complete=true`；崩溃后按 `.part` **实际文件长度**作为已下载前缀（T8.5 用 Range 续传）；`.part` 与 meta 不一致时按文件长度校正，不拼接双份。
6. **损坏检测/重建**：meta 缺失/非法/大小不符 → 标 broken；`Rebuild` 只删该条目数据（`.part`/`.mp4`/meta），不删在线记录/截图；重建后不把坏数据当可播放。
7. **清空/占用**：占用可见（总/单条）；`ClearAll` 只删缓存根下的条目，**不删在线记录/截图**（T8.7 的 `%APPDATA%\拉片软件\online`、图片目录）；不提供导出/分享入口。
8. **孤儿清理**：无 meta 的 `.part`、超过 24h 未访问的 `complete=false` 条目可清理（可配置/可关）；**不清理当前会话**；不删除用户源视频/在线记录。
9. **路径/配额跨会话**：修改缓存根/配额后，新进程仍生效；路径变化不迁移旧缓存（只提示，不搬）；旧缓存仍在旧根。
10. **测试/证据**：
    - `lapian_online_cache_test`（ctest，纯存储；300MB 配额下的 LRU 顺序/当前不淘汰/总占用 ≤ 配额；`.part`→`.mp4`；续传长度；损坏/重建；清空保留记录；磁盘满 ≤5s；路径/配额跨会话；Unicode/长路径；无明文扫描）。
    - `--online-cache-test`（无 GUI，临时根，合成文件；**不访问网络、不需要 SESSDATA**）；输出 root/quota/usage/entries/LRU/evicted/protected/resume/corrupt/rebuild/clear/records_kept/space_error_ms/verdict。
    - `docs/perf/v2-online-cache.md`：命令、根/配额、300MB 表、LRU 顺序、当前保护、续传、损坏、磁盘满、清空、跨会话、长路径/Unicode、缺口。
    - `docs/handoff-YYYY-MM-DD-t8.3.md`：改了什么、接口、测试数字、缺口、T8.4/T8.5 需要。
11. **回归**：`ctest`（当前 24/24，新增 1 个应为 25/25）、`--online-resolve-test` 156/0、`--online-auth-test` 54/0、本地 P1/P2/P7、dist 导入表白名单；T8.2 的 nav 自检可选（需要 `.secrets`，T8.3 不依赖账号）。

## 硬约束 / 门

- **不做 T8.4**：不实现 `GrowingSource`、`Cursor`、AVIO、多读者、阻塞读、取消唤醒；只留 `CacheStore` 接口给 T8.4。
- **不做 T8.5**：不发 `playurl`、不下载 CDN、不做 Range GET/重试/备份 URL/URL 过期刷新；T8.3 只做本地存储。
- **不做 T8.6/T8.7**：不做在线会话 UI、缓冲进度、缓存命中打开、在线标记/截图；不加“清缓存”大 UI。
- **不碰 T8.2**：不改 nav/凭据/Cookie 逻辑；不把 SESSDATA/Cookie 写进 cache/meta/log；T8.3 不需要网络。
- 缓存根默认 `%LOCALAPPDATA%`，**不在源视频目录**；不导出、不分享、不再分发。
- 配额/验收用 **300MB 小配额** 验 LRU/淘汰/占用/磁盘满，再补 **20GB 记账单测**（不真填满）；不写“未实测的 20GB 真填满”。
- 本地模式零网络不退化（O9）；不新增 QtNetwork/WinHTTP/TLS 导入；`check-imports.ps1` 必须继续通过。
- 不改 `spec.md`/`tasks.md`/v1 冻结文件；不改 delta 数字/M1/M2 门；不把 4K/1080P60 写成 MVP 通过；不写未缓存区 80ms。
- 长路径/中文/文件锁/杀软：至少覆盖 Unicode 路径和 >MAX_PATH 的原子改名；`MoveFileExW` 需要 `\\?\` 前缀（可参考 `MarkStore.cpp` 的 `NativeLongPath`，但不要改 v1 sidecar schema）。
- 一个任务 = 一次可审查提交；完成后提交一个 commit，不要和 T8.4/T8.5 混在一起；`.secrets/` 不入库。

## 建议实现（可调整，但要保持小步）

- 新增 `src/online/CacheStore.h/.cpp`：`CacheKey{bvid,cid,qn,codecid}`、`CacheEntry`、`CacheStore`。
  ```cpp
  class CacheStore {
   public:
    explicit CacheStore(QString root = QString(), qint64 quota_bytes = 0);
    static QString DefaultRoot();          // %LOCALAPPDATA%\拉片软件\cache
    static qint64 DefaultQuotaBytes();     // 20 GiB
    QString root() const; qint64 quota() const;
    bool SetRoot(const QString&, QString* error);
    bool SetQuotaBytes(qint64, QString* error);
    qint64 UsageBytes() const;
    bool Reserve(const CacheKey&, qint64 expected_bytes, QString* error); // 空间 + LRU
    bool OpenPart(const CacheKey&, QString* part_path, qint64* existing_bytes,
                  QString* error);        // 续传；不请求网络
    bool UpdateProgress(const CacheKey&, qint64 downloaded_bytes, bool force);
    bool MarkComplete(const CacheKey&, QString* error); // .part -> .mp4
    std::optional<CacheEntry> Load(const CacheKey&, QString* error);
    bool OpenComplete(const CacheKey&, QString* mp4_path, QString* error); // T8.7 用
    bool Scan(QVector<CacheEntry>* out, QVector<CacheEntry>* broken);
    bool EvictToQuota(const CacheKey& current, QString* error);
    bool Rebuild(const CacheKey&, QString* error);
    bool ClearAll(QString* error);
    bool CleanupOrphans(const CacheKey& current, QString* error);
  };
  ```
- `meta.json` 原子写用 `QSaveFile`；`.part`→`.mp4` 用 `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` + `\\?\`；失败保留 `.part` 并给可读错误。
- `Reserve` 先 `GetDiskFreeSpaceExW(root)` 检查 `expected + 512MB`，再按 `last_access_at` 淘汰非当前条目；当前条目永不淘汰；仍不足返回配额/磁盘错误。
- `UsageBytes` 按 meta + 文件实际大小；`Scan` 不因单条损坏中断；损坏条目标 broken。
- `SettingsStore` 扩展：给 `AppSettings` 增加 `cache_root`（空=默认）和 `cache_quota_gb`（默认 20，clamp 1–100）；旧 `settings.json` 无键时用默认；`lapian_app_settings_test` 补字段；**不改 v1 现有键语义**。若你觉得独立 `CacheSettings` 更干净，可以，但必须跨会话且不污染 v1 schema。
- `--online-cache-test` 放在 `src/app/OnlineCacheTestRunner.*`，在 `main.cpp` 的 `QApplication`/`MainWindow` 之前分流（无 GUI）；临时根 `build/t8.3-cache-<pid>`，结束清理；不要碰真实 `%LOCALAPPDATA%` 缓存。
- 测试用 `QFile::resize` 造稀疏大文件，避免真写 300MB/20GB；`UsageBytes` 用文件大小而非实际磁盘簇；20GB 只做记账/淘汰单测。
- 清空测试：在临时“在线记录”目录放 sentinel 文件，`ClearAll` 后 assert 仍在；缓存测试不创建 T8.7 的真实记录。
- 长路径测试：临时根下构造 >260 字符的 `bvid/cid/qn-codecid` 路径，验证创建/改名/读取；若环境不支持，记录 skip 并给出原因。

## 构建 / 测试命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"

# 本机 CMake 4.2/3.31 读取已有 cache 会崩溃（0xC0000409）；用项目脚本/3.31.6
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

build\release\bin\lapian.exe --online-cache-test
build\release\bin\lapian.exe --online-resolve-test
build\release\bin\lapian.exe --online-auth-test
build\release\bin\lapian.exe --startup-test --file media\proxy\proxy-V1.mp4
build\release\bin\lapian.exe --gop-boundary-test --file build\test-media\real-gop2s-full.mp4 --samples 20

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -SkipTests
dist\lapian.exe --online-resolve-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1
```

- T8.3 不需要 SESSDATA；`.secrets\bilibili_sessdata.txt` 仍可留作 T8.2 可选回归，**不要打印/提交**。
- 当前 `ctest` 24/24（T8.3 新增后应为 25/25）；`--online-resolve-test` 156/0；`--online-auth-test` 54/0；本地 P1/P2/P7 约 267.83/324.17/66.64ms（不是验收数字，只作回归基线）。

## 交付

- `src/online/CacheStore.*`、必要的 `AppSettings` 扩展、`src/app/OnlineCacheTestRunner.*`、`tests/online_cache_test.cpp`、CMake 接线。
- `--online-cache-test`；`docs/perf/v2-online-cache.md`；`docs/handoff-YYYY-MM-DD-t8.3.md`。
- 更新 `tasks-v2.md` T8.3 状态、`AGENTS.md` v2 状态/文档表；不改 M1/M2 门与 delta 数字。
- 一个 task commit；最终报告按 5 段：改动文件/接口；测试命令/结果；证据路径；未解缺口；T8.4/T8.5 需要什么。

## 停止条件（任一出现就停，不要猜）

1. 缓存根无法读写/无法原子改名（ACL、长路径、文件锁），且没有安全的降级方案；
2. 磁盘可用空间/配额无法可靠计算，或 300MB/20GB 测试只能靠真实填满磁盘；
3. 实现需要改 O9 本地零网络、M1/M2 门或 delta 数字；
4. 需要新增网络请求/Cookie/签名 URL 才能完成 T8.3；
5. 发现 T8.4/T8.5 的前置条件与 tasks-v2 不一致（例如 T8.3 被迫实现读层/下载）。

遇到上述情况：写 blocked 证据、已完成部分和下一步；问用户，不要用假数据/跳过测试标 T8.3 完成。
