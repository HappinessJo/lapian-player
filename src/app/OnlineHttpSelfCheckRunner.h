#pragma once

// T8.2 无 GUI 的 HTTP 栈自检（--online-http-selfcheck）。
//
// - 产品实际栈：WinHTTP（首选）；libcurl 仅在构建时定义 LAPIAN_HAVE_LIBCURL；
// - 从 .secrets\bilibili_sessdata.txt 或 LAPIAN_SESSDATA_FILE 读取 SESSDATA，
//   不接受命令行明文 secret；
// - 只用带 SESSDATA 的 nav 判断 code=0/isLogin=true；
// - 输出 stack/code/is_login/elapsed_ms/http_status/error（可加 cookie_sent、
//   vip_status 作证据，均不含 secret）；
// - 退出码：0 = 至少一种栈通过；1 = 都不通过；2 = 环境/参数/secret 缺失。

#include <QString>

namespace lapian::app {

struct OnlineHttpSelfCheckOptions {
  QString stack = QStringLiteral("auto");
  QString sessdata_file;
  int timeout_ms = 2000;
  bool verbose = false;
};

int RunOnlineHttpSelfCheck(const OnlineHttpSelfCheckOptions& options);

}  // namespace lapian::app
