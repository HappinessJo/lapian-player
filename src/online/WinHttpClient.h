#pragma once

// T8.1 最小匿名 WinHTTP 实现（T8.2 起支持显式 SESSDATA）。
//
// 关键点：
// - **动态加载 winhttp.dll**：发布包不新增 WINHTTP.DLL 导入，本地模式启动不加载
//   网络栈（O9）；也在 v1 的 check-imports.ps1 白名单外，不污染 dist。
// - 异步 WinHTTP + 工作线程等待；取消由工作线程关请求句柄完成，避免
//   “从别的线程关同步 WinHTTP 句柄”的竞态。
// - T8.2 只允许显式 SESSDATA：仅 api.bilibili.com:443 + HTTPS，手动加
//   `Cookie: SESSDATA=...`，禁用 WinHTTP cookie jar，且不跟随重定向。
// - 不打印 headers/Cookie/URL 查询串；日志只记 host/path/status/耗时。

#include "online/IHttpClient.h"

#ifndef _WIN32
#include "online/CurlHttpClient.h"
#endif

namespace lapian::online {

#ifdef _WIN32
class WinHttpClient final : public IHttpClient {
 public:
  WinHttpClient() = default;
  ~WinHttpClient() override = default;

  HttpResponse Get(const HttpRequest& request,
                   const HttpCancelToken& cancel) override;

  // 测试/证据用：winhttp.dll 是否已被本进程动态加载（应只在首次解析后为真）。
  static bool LibraryLoadedForTest();

  // 测试用：只解析 URL 并返回可读错误，不发起请求；避免测试依赖外网。
  static QString ValidateUrlForTest(const QString& url);

  // 测试用：带 SESSDATA 的请求是否允许发往该 URL（只允许 api.bilibili.com:443）。
  static bool CookieAllowedForTest(const QString& url);
};
#else
using WinHttpClient = CurlHttpClient;
#endif

}  // namespace lapian::online
