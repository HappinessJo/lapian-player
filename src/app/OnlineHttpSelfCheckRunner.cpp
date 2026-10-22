#include "app/OnlineHttpSelfCheckRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QByteArray>

#include <cstdio>
#include <cstdlib>

#include "online/BiliAuth.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

constexpr qint64 kMaxSecretBytes = 8 * 1024;

struct SecretFile {
  bool ok = false;
  QString path;
  QString source;
  QString sessdata;
  QString error;
};

QString ResolveSecretPath(const OnlineHttpSelfCheckOptions& options,
                          QString* source) {
  if (!options.sessdata_file.isEmpty()) {
    *source = QStringLiteral("arg");
    return options.sessdata_file;
  }
  const QString env = qEnvironmentVariable("LAPIAN_SESSDATA_FILE");
  if (!env.isEmpty()) {
    *source = QStringLiteral("env");
    return env;
  }
  *source = QStringLiteral("default");
  const QStringList candidates = {
      QDir::current().filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../.secrets/bilibili_sessdata.txt")),
  };
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return candidates.first();
}

SecretFile ReadSecret(const OnlineHttpSelfCheckOptions& options) {
  SecretFile secret;
  secret.path = ResolveSecretPath(options, &secret.source);
  const QFileInfo info(secret.path);
  if (!info.exists() || !info.isFile()) {
    secret.error = QStringLiteral("missing_sessdata_file");
    return secret;
  }
  if (info.size() <= 0 || info.size() > kMaxSecretBytes) {
    secret.error = QStringLiteral("invalid_sessdata_file_size");
    return secret;
  }
  QFile file(secret.path);
  if (!file.open(QIODevice::ReadOnly)) {
    secret.error = QStringLiteral("unreadable_sessdata_file");
    return secret;
  }
  const QByteArray raw = file.readAll();
  file.close();
  const lapian::online::SessdataNormalization normalized =
      lapian::online::NormalizeSessdata(QString::fromUtf8(raw));
  if (!normalized.ok) {
    secret.error = QStringLiteral("invalid_sessdata:") + normalized.error;
    return secret;
  }
  secret.ok = true;
  secret.sessdata = normalized.value;
  return secret;
}

void PrintResult(const QString& stack, const lapian::online::AuthResult& result) {
  std::printf(
      "stack=%s code=%d is_login=%d http_status=%d elapsed_ms=%lld "
      "cookie_sent=%d vip_status=%d error=%s",
      stack.toUtf8().constData(), result.api_code, result.is_login ? 1 : 0,
      result.http_status, static_cast<long long>(result.elapsed_ms),
      result.request_cookie_sent ? 1 : 0, result.vip_status,
      lapian::online::AuthErrorName(result.error).toUtf8().constData());
  if (!result.message.isEmpty()) {
    std::printf(" message=\"%s\"", result.message.toUtf8().constData());
  }
  std::printf("\n");
  std::fflush(stdout);
}

#ifdef LAPIAN_HAVE_LIBCURL
lapian::online::AuthResult RunLibcurl(const QString& sessdata,
                                      int timeout_ms) {
  // T8.16 打包前才会真正引入 libcurl；T8.2 先保留产品实际栈的位置。
  // 若启用，必须使用 libcurl 的 CA 校验（CURLOPT_SSL_VERIFYPEER=1），
  // 只把 Cookie 加到 api.bilibili.com，并禁止重定向携带 Cookie。
  lapian::online::AuthResult result;
  result.state = lapian::online::AuthState::kLoginUnavailable;
  result.error = lapian::online::AuthError::kNetwork;
  result.message = QStringLiteral("libcurl 客户端尚未实现；将按未登录清晰度播放");
  result.detail = QStringLiteral("libcurl_placeholder timeout_ms=%1").arg(timeout_ms);
  (void)sessdata;
  return result;
}
#endif

}  // namespace

int RunOnlineHttpSelfCheck(const OnlineHttpSelfCheckOptions& options) {
  using lapian::online::AuthResult;
  using lapian::online::CheckNav;
  using lapian::online::HttpCancelToken;

  if (options.verbose) {
    qputenv("LAPIAN_ONLINE_LOG", "1");
  }

  const QString stack = options.stack.trimmed().toLower();
  if (stack != QStringLiteral("winhttp") && stack != QStringLiteral("libcurl") &&
      stack != QStringLiteral("auto")) {
    std::printf("error=unknown_stack stack=%s\n", stack.toUtf8().constData());
    return 2;
  }

  const SecretFile secret = ReadSecret(options);
  if (!secret.ok) {
    std::printf("error=%s secret_source=%s\n", secret.error.toUtf8().constData(),
                secret.source.toUtf8().constData());
    return 2;
  }

  std::printf("T8.2 online_http_selfcheck stack=%s timeout_ms=%d secret_source=%s\n",
              stack.toUtf8().constData(), options.timeout_ms,
              secret.source.toUtf8().constData());

  bool passed = false;
  bool infrastructure_error = false;

  auto run_winhttp = [&]() {
    lapian::online::WinHttpClient client;
    HttpCancelToken cancel;
    const AuthResult result =
        CheckNav(secret.sessdata, &client, cancel, options.timeout_ms);
    PrintResult(QStringLiteral("winhttp"), result);
    if (result.ok()) {
      passed = true;
    }
  };

  auto run_libcurl = [&]() {
#ifdef LAPIAN_HAVE_LIBCURL
    const AuthResult result = RunLibcurl(secret.sessdata, options.timeout_ms);
    PrintResult(QStringLiteral("libcurl"), result);
    if (result.ok()) {
      passed = true;
    }
#else
    std::printf("stack=libcurl status=not_built\n");
    if (stack == QStringLiteral("libcurl")) {
      infrastructure_error = true;
    }
#endif
  };

  if (stack == QStringLiteral("winhttp") || stack == QStringLiteral("auto")) {
    run_winhttp();
  }
  if (stack == QStringLiteral("libcurl") || (stack == QStringLiteral("auto") && !passed)) {
    run_libcurl();
  }

  std::printf("verdict=%s\n", passed ? "pass" : "fail");
  std::fflush(stdout);
  if (passed) {
    return 0;
  }
  return infrastructure_error ? 2 : 1;
}

}  // namespace lapian::app
