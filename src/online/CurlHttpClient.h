#pragma once

#include "online/IHttpClient.h"

namespace lapian::online {

class CurlHttpClient final : public IHttpClient {
 public:
  CurlHttpClient() = default;
  ~CurlHttpClient() override = default;

  HttpResponse Get(const HttpRequest& request,
                   const HttpCancelToken& cancel) override;

  static bool LibraryLoadedForTest();
  static QString ValidateUrlForTest(const QString& url);
  static bool CookieAllowedForTest(const QString& url);
};

}  // namespace lapian::online
