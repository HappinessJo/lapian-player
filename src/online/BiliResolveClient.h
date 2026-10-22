#pragma once

// T8.1 解析客户端：本地校验 → 匿名 view GET → JSON 解析 → 分P选择。
// 不下载媒体、不取 playurl、不碰 UI；为 T8.2（HTTP 自检）与 T8.5（取流）
// 留出可注入的 IHttpClient。

#include "online/BiliResolver.h"
#include "online/IHttpClient.h"

namespace lapian::online {

struct BiliResolveResult {
  BiliVideo video;
  BiliResolveError error;
  int http_requests = 0;
  qint64 elapsed_ms = 0;
};

class BiliResolveClient {
 public:
  explicit BiliResolveClient(IHttpClient* http) : http_(http) {}

  // 同步执行；调用方负责放到工作线程。cancel 可在其它线程置位。
  BiliResolveResult Resolve(const QString& input,
                            const HttpCancelToken& cancel);

  int request_count() const { return request_count_; }

 private:
  IHttpClient* http_ = nullptr;
  int request_count_ = 0;
};

}  // namespace lapian::online
