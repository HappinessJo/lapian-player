#pragma once

// T8.5 CDN 取流传输层：单个 Range GET、流式写入 sink、可取消、无 Cookie。
//
// 边界：
// - 只做“从 offset 取到文件末尾”；重试/备份/刷新/写 .part 由 DownloadJob 负责；
// - **绝不携带 Cookie/SESSDATA**，也不使用 WinHTTP cookie jar；
// - 只允许 HTTPS；校验证书；不跟随重定向（30x 直接失败）；
// - 不打印完整 URL（签名参数），日志只记 host/path/status/耗时；
// - 可注入 fake transport，单测/离线探针不联网。
//
// Range 语义（由 DownloadJob 依赖）：
// - offset==0：200 或 206 都接受；总大小来自 Content-Length/Content-Range；
// - offset>0：必须是 206 且 Content-Range 起点 == offset；否则返回
//   `range_ignored`（200）或 `start_mismatch`；
// - Content-Range 总长与 expected_total 不一致 → `total_mismatch`，不写盘。

#include <QString>

#include <cstdint>

#include "online/IHttpClient.h"

namespace lapian::online {

enum class DownloadTransportStatus {
  kOk = 0,
  kCancelled,
  kTimeout,
  kNetwork,
  kHttpStatus,
  kRangeIgnored,      // 服务端忽略 Range，返回 200 全量
  kRangeStartMismatch,  // 206 但 Content-Range 起点 != offset
  kTotalMismatch,     // Content-Range/Content-Length 总长与已知总大小不一致
  kInvalidUrl,
  kSinkError,         // 写 .part 失败（磁盘满/IO；headers_only 探测不会产生）
};

QString DownloadTransportStatusName(DownloadTransportStatus status);

// 流式写入目标。Write 返回 false 表示磁盘/IO 失败，transport 立即停止。
class IDownloadSink {
 public:
  virtual ~IDownloadSink() = default;
  virtual bool Write(const std::uint8_t* data, std::size_t size,
                     QString* error) = 0;
};

struct DownloadRequest {
  QString url;
  std::int64_t offset = 0;
  // T8.10：闭区间结束偏移；-1 表示“取到文件末尾”（T8.5 行为不变）。
  // 只用于 DASH 总字节探测（bytes=offset-end），下载仍用 -1。
  std::int64_t range_end = -1;
  // P0 收口：只要响应头（DASH 总字节探测）。置位后不读 body、不写 sink，
  // 校验完 Content-Length/Content-Range 即成功返回；避免把“探测端主动停止”
  // 误报成 `kSinkError` 的“写入缓存文件失败”。下载路径必须保持 false。
  bool headers_only = false;
  // 已知总字节（playurl size）；<=0 表示未知。>0 时 transport 会对账。
  std::int64_t expected_total = -1;
  // 单次尝试的总上限（DNS/连接/读取共享）。
  int timeout_ms = 20000;
  QString user_agent;
  QString referer;
};

struct DownloadTransportResult {
  DownloadTransportStatus status = DownloadTransportStatus::kOk;
  bool ok = false;
  bool cancelled = false;
  bool timed_out = false;
  int http_status = 0;

  std::int64_t content_length = -1;
  std::int64_t content_range_start = -1;
  std::int64_t content_range_end = -1;
  std::int64_t content_range_total = -1;
  std::int64_t bytes_written = 0;

  bool range_ignored = false;
  bool start_mismatch = false;
  bool total_mismatch = false;

  std::int64_t first_byte_ms = -1;
  std::int64_t elapsed_ms = 0;
  QString error;
  int win32_error = 0;
};

class IDownloadTransport {
 public:
  virtual ~IDownloadTransport() = default;
  virtual DownloadTransportResult GetRange(const DownloadRequest& request,
                                           IDownloadSink* sink,
                                           const HttpCancelToken& cancel) = 0;
};

// Windows 产品栈：动态加载 WinHTTP；异步读取 + 工作线程轮询取消。
class WinHttpDownloadTransport final : public IDownloadTransport {
 public:
  WinHttpDownloadTransport() = default;
  ~WinHttpDownloadTransport() override = default;

  DownloadTransportResult GetRange(const DownloadRequest& request,
                                   IDownloadSink* sink,
                                   const HttpCancelToken& cancel) override;

  // 测试/证据：winhttp.dll 是否已被本进程动态加载。
  static bool LibraryLoadedForTest();
};

}  // namespace lapian::online
