#include "app/OnlineQrTestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "online/CredentialStore.h"
#include "online/IHttpClient.h"
#include "online/QrCode.h"
#include "online/QrLogin.h"
#include "online/QrLoginController.h"
#include "online/WinHttpClient.h"

#include "app/OnlineResolveDialog.h"

namespace lapian::app {
namespace {

using lapian::online::AuthState;
using lapian::online::CredentialStore;
using lapian::online::HttpCancelToken;
using lapian::online::HttpRequest;
using lapian::online::HttpResponse;
using lapian::online::IHttpClient;
using lapian::online::QrImageResult;
using lapian::online::QrLoginController;
using lapian::online::QrLoginState;
using lapian::online::QrPollStatus;

int g_checks = 0;
int g_failures = 0;

void Check(const QString& name, bool ok, const QString& detail = QString()) {
  ++g_checks;
  if (ok) {
    std::printf("[ OK ] T8.2.2 %s\n", name.toUtf8().constData());
  } else {
    ++g_failures;
    std::printf("[FAIL] T8.2.2 %s%s%s\n", name.toUtf8().constData(),
                detail.isEmpty() ? "" : "：", detail.toUtf8().constData());
  }
  std::fflush(stdout);
}

void PrintKV(const char* key, const QString& value) {
  std::printf("%s=%s\n", key, value.toUtf8().constData());
}

QString Num(qint64 value) { return QString::number(value); }

QByteArray MakeGenerateBody(const QString& key) {
  QJsonObject data;
  data.insert(QStringLiteral("url"),
              QStringLiteral(
                  "https://account.bilibili.com/h5/account-h5/auth/scan-web"));
  data.insert(QStringLiteral("qrcode_key"), key);
  QJsonObject root;
  root.insert(QStringLiteral("code"), 0);
  root.insert(QStringLiteral("message"), QStringLiteral("OK"));
  root.insert(QStringLiteral("data"), data);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MakePollBody(int data_code, const QString& url = QString()) {
  QJsonObject data;
  data.insert(QStringLiteral("code"), data_code);
  data.insert(QStringLiteral("message"), QString());
  data.insert(QStringLiteral("url"), url);
  QJsonObject root;
  root.insert(QStringLiteral("code"), 0);
  root.insert(QStringLiteral("message"), QStringLiteral("OK"));
  root.insert(QStringLiteral("data"), data);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MakeNavBody(bool login, int code = 0) {
  QJsonObject data;
  data.insert(QStringLiteral("isLogin"), login);
  data.insert(QStringLiteral("vipStatus"), 0);
  QJsonObject root;
  root.insert(QStringLiteral("code"), code);
  root.insert(QStringLiteral("message"), QStringLiteral("OK"));
  root.insert(QStringLiteral("data"), data);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString SuccessUrl(const QString& sessdata) {
  return QStringLiteral(
             "https://www.bilibili.com/?SESSDATA=%1&bili_jct=FAKE_CSRF&"
             "refresh_token=FAKE_REFRESH&DedeUserID=42")
      .arg(sessdata);
}

const QString kKey1 = QStringLiteral("0123456789abcdef0123456789abcdef");
const QString kKey2 = QStringLiteral("fedcba9876543210fedcba9876543210");
const QString kSessdata = QStringLiteral("FAKE_SESSDATA_T822_SENTINEL");

struct FakeQrHttp final : IHttpClient {
  QByteArray generate_body;
  int generate_status = 200;
  QVector<QByteArray> poll_bodies;
  int poll_status = 200;
  QByteArray nav_body;
  bool block_poll = false;
  QString cookie_sessdata;

  QVector<HttpRequest> requests;
  int generate_requests = 0;
  int poll_requests = 0;
  int nav_requests = 0;
  int poll_index = 0;

  HttpResponse Get(const HttpRequest& request,
                   const HttpCancelToken& cancel) override {
    requests.append(request);
    const QString path = QUrl(request.url).path();
    if (path.contains(QStringLiteral("qrcode/generate"))) {
      ++generate_requests;
      return Reply(generate_status, generate_body);
    }
    if (path.contains(QStringLiteral("qrcode/poll"))) {
      ++poll_requests;
      if (block_poll) {
        while (!cancel.IsCancelled()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        HttpResponse response;
        response.cancelled = true;
        response.error = QStringLiteral("cancelled");
        return response;
      }
      QByteArray body;
      if (!poll_bodies.isEmpty()) {
        const int index =
            std::min(poll_index, static_cast<int>(poll_bodies.size()) - 1);
        body = poll_bodies.at(index);
        if (poll_index < static_cast<int>(poll_bodies.size()) - 1) {
          ++poll_index;
        }
      }
      HttpResponse response = Reply(poll_status, body);
      if (response.ok) {
        response.response_sessdata = cookie_sessdata;
      }
      return response;
    }
    if (path.contains(QStringLiteral("web-interface/nav"))) {
      ++nav_requests;
      return Reply(200, nav_body);
    }
    return Reply(404, QByteArray());
  }

 private:
  static HttpResponse Reply(int status, const QByteArray& body) {
    HttpResponse response;
    response.http_status = status;
    response.ok = status >= 200 && status < 300;
    response.body = body;
    if (!response.ok) {
      response.error = QStringLiteral("HTTP %1").arg(status);
    }
    return response;
  }
};

void Spin(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

bool WaitForState(QrLoginController* controller, QrLoginState state,
                  int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (controller->state() == state) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return controller->state() == state;
}

bool ModuleDark(const QImage& image, int module_x, int module_y, int scale,
                int border) {
  const int x = (module_x + border) * scale + scale / 2;
  const int y = (module_y + border) * scale + scale / 2;
  if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
    return false;
  }
  const QRgb pixel = image.pixel(x, y);
  return qRed(pixel) < 128 && qGreen(pixel) < 128 && qBlue(pixel) < 128;
}

bool FinderPatternAt(const QImage& image, int module_x, int module_y, int scale,
                     int border) {
  for (int y = 0; y < 7; ++y) {
    for (int x = 0; x < 7; ++x) {
      const bool border_cell = x == 0 || y == 0 || x == 6 || y == 6;
      const bool center_cell = x >= 2 && x <= 4 && y >= 2 && y <= 4;
      if ((border_cell || center_cell) !=
          ModuleDark(image, module_x + x, module_y + y, scale, border)) {
        return false;
      }
    }
  }
  return true;
}

bool RunRenderChecks() {
  const QrImageResult rendered = lapian::online::RenderQrImage(
      QStringLiteral("https://account.bilibili.com/h5/account-h5/auth/scan-web"),
      6, 4);
  const bool ok = rendered.ok() && rendered.version >= 1 &&
                  rendered.modules >= 21 &&
                  rendered.image.width() == (rendered.modules + 8) * 6 &&
                  FinderPatternAt(rendered.image, 0, 0, 6, 4) &&
                  FinderPatternAt(rendered.image, rendered.modules - 7, 0, 6, 4) &&
                  FinderPatternAt(rendered.image, 0, rendered.modules - 7, 6, 4);
  Check(QStringLiteral("qr render/finders"), ok);
  PrintKV("qr_version", Num(rendered.version));
  PrintKV("qr_modules", Num(rendered.modules));
  PrintKV("qr_image_px", Num(rendered.image.width()));
  PrintKV("qr_quiet_zone", QStringLiteral("4"));
  Check(QStringLiteral("qr rejects empty"),
        !lapian::online::RenderQrImage(QString()).ok());
  return ok;
}

bool RunSuccess(bool cookie_only) {
  QTemporaryDir temp;
  CredentialStore credentials(
      QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
  FakeQrHttp http;
  http.generate_body = MakeGenerateBody(kKey1);
  http.poll_bodies = {MakePollBody(86101), MakePollBody(86090),
                      MakePollBody(0, cookie_only ? QString()
                                                  : SuccessUrl(kSessdata))};
  http.cookie_sessdata = cookie_only ? kSessdata : QString();
  http.nav_body = MakeNavBody(true);

  QrLoginController controller(&http, &credentials);
  controller.SetPollIntervalForTest(30);
  controller.SetExpiryMsForTest(5000);
  controller.SetNavTimeoutForTest(1000);
  QStringList states;
  int success_signals = 0;
  QObject::connect(&controller, &QrLoginController::stateChanged,
                   [&states](int state, const QString&) {
                     states.append(lapian::online::QrLoginStateName(
                         static_cast<QrLoginState>(state)));
                   });
  QObject::connect(&controller, &QrLoginController::loginSucceeded,
                   [&success_signals]() { ++success_signals; });
  controller.Start();
  const bool success = WaitForState(&controller, QrLoginState::kSuccess, 5000);
  Check(cookie_only ? QStringLiteral("cookie-only success")
                    : QStringLiteral("url success"),
        success && success_signals == 1);
  Check(QStringLiteral("nav gate result"),
        controller.authResult().state == AuthState::kLoggedIn);
  const lapian::online::CredentialLoadResult loaded = credentials.Load();
  Check(QStringLiteral("only SESSDATA saved"),
        loaded.has_credential && loaded.sessdata == kSessdata);
  PrintKV(cookie_only ? "cookie_flow" : "success_flow",
          success ? QStringLiteral("pass") : QStringLiteral("fail"));
  PrintKV("state_sequence", states.join(QLatin1Char(',')));
  PrintKV("generate_requests", Num(http.generate_requests));
  PrintKV("poll_requests", Num(http.poll_requests));
  PrintKV("nav_requests", Num(http.nav_requests));
  bool no_cookie = true;
  for (const HttpRequest& request : http.requests) {
    if (QUrl(request.url).path().contains(QStringLiteral("qrcode")) &&
        !request.sessdata.isEmpty()) {
      no_cookie = false;
    }
  }
  Check(QStringLiteral("QR requests carry no cookie"), no_cookie);
  return success && loaded.has_credential && loaded.sessdata == kSessdata;
}

bool RunNavFailure() {
  QTemporaryDir temp;
  CredentialStore credentials(
      QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
  FakeQrHttp http;
  http.generate_body = MakeGenerateBody(kKey1);
  http.poll_bodies = {MakePollBody(0, SuccessUrl(kSessdata))};
  http.nav_body = MakeNavBody(false, -101);
  QrLoginController controller(&http, &credentials);
  controller.SetPollIntervalForTest(30);
  controller.SetExpiryMsForTest(5000);
  controller.SetNavTimeoutForTest(1000);
  int success_signals = 0;
  QObject::connect(&controller, &QrLoginController::loginSucceeded,
                   [&success_signals]() { ++success_signals; });
  controller.Start();
  const bool failed = WaitForState(&controller, QrLoginState::kFailed, 5000);
  Check(QStringLiteral("nav -101 failure"), failed && success_signals == 0);
  Check(QStringLiteral("nav -101 no credential save"),
        !credentials.Load().has_credential);
  PrintKV("nav_gate", failed ? QStringLiteral("pass") : QStringLiteral("fail"));
  return failed;
}

bool RunExpiry() {
  QTemporaryDir temp;
  CredentialStore credentials(
      QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
  FakeQrHttp http;
  http.generate_body = MakeGenerateBody(kKey1);
  http.poll_bodies = {MakePollBody(86101)};
  QrLoginController controller(&http, &credentials);
  controller.SetPollIntervalForTest(30);
  controller.SetExpiryMsForTest(250);
  controller.Start();
  const bool expired = WaitForState(&controller, QrLoginState::kExpired, 5000);
  const int polls_at_expiry = http.poll_requests;
  Spin(300);
  const bool stopped = http.poll_requests == polls_at_expiry;
  Check(QStringLiteral("expiry stops poll"),
        expired && stopped && controller.qrKeyForTest().isEmpty());
  http.generate_body = MakeGenerateBody(kKey2);
  controller.Refresh();
  const bool refreshed =
      WaitForState(&controller, QrLoginState::kWaiting, 3000) &&
      controller.qrKeyForTest() == kKey2 && http.generate_requests == 2;
  Check(QStringLiteral("expiry refresh new key"), refreshed);
  controller.Cancel();
  PrintKV("expiry", expired && stopped ? QStringLiteral("pass")
                                       : QStringLiteral("fail"));
  PrintKV("refresh", refreshed ? QStringLiteral("pass") : QStringLiteral("fail"));
  return expired && stopped && refreshed;
}

bool RunCancel() {
  QTemporaryDir temp;
  CredentialStore credentials(
      QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
  FakeQrHttp http;
  http.generate_body = MakeGenerateBody(kKey1);
  http.poll_bodies = {MakePollBody(86101)};
  http.block_poll = true;
  QrLoginController controller(&http, &credentials);
  controller.SetPollIntervalForTest(30);
  controller.SetExpiryMsForTest(5000);
  controller.Start();
  const bool waiting = WaitForState(&controller, QrLoginState::kWaiting, 3000);
  Spin(100);
  QElapsedTimer timer;
  timer.start();
  controller.Cancel();
  const qint64 cancel_ms = timer.elapsed();
  const int polls_after = http.poll_requests;
  Spin(200);
  const bool stopped = http.poll_requests == polls_after;
  Check(QStringLiteral("cancel <=100ms"),
        waiting && cancel_ms <= 100 &&
            controller.state() == QrLoginState::kCancelled && stopped);
  PrintKV("cancel_ms", Num(cancel_ms));
  PrintKV("cancel", waiting && stopped ? QStringLiteral("pass")
                                       : QStringLiteral("fail"));
  return waiting && cancel_ms <= 100 && stopped;
}

bool RunFailures() {
  bool ok = true;
  {
    QTemporaryDir temp;
    CredentialStore credentials(
        QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
    FakeQrHttp http;
    http.generate_body = QByteArray("{bad");
    QrLoginController controller(&http, &credentials);
    controller.Start();
    const bool failed = WaitForState(&controller, QrLoginState::kFailed, 3000);
    Check(QStringLiteral("bad generate json"), failed);
    ok = ok && failed;
  }
  {
    QTemporaryDir temp;
    CredentialStore credentials(
        QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
    FakeQrHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {QByteArray("{bad")};
    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(30);
    controller.Start();
    const bool failed = WaitForState(&controller, QrLoginState::kFailed, 4000);
    Check(QStringLiteral("bad poll json"), failed);
    ok = ok && failed;
  }
  {
    QTemporaryDir temp;
    CredentialStore credentials(
        QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
    FakeQrHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.generate_status = 412;
    QrLoginController controller(&http, &credentials);
    controller.Start();
    const bool failed = WaitForState(&controller, QrLoginState::kFailed, 3000);
    Check(QStringLiteral("generate 412"), failed);
    ok = ok && failed;
  }
  {
    QTemporaryDir temp;
    CredentialStore credentials(
        QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
    FakeQrHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {MakePollBody(86101)};
    http.poll_status = 429;
    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(30);
    controller.Start();
    const bool failed = WaitForState(&controller, QrLoginState::kFailed, 4000);
    Check(QStringLiteral("poll 429 no retry"), failed && http.poll_requests == 1);
    ok = ok && failed && http.poll_requests == 1;
  }
  PrintKV("bad_json_412_429", ok ? QStringLiteral("pass")
                                 : QStringLiteral("fail"));
  return ok;
}

bool RunUiSmoke() {
  QTemporaryDir temp;
  OnlineResolveDialog dialog;
  dialog.SetCredentialPathForTest(
      QDir(temp.path()).filePath(QStringLiteral("credentials.dat")));
  auto fake = std::make_unique<FakeQrHttp>();
  fake->generate_body = MakeGenerateBody(kKey1);
  fake->poll_bodies = {MakePollBody(86101), MakePollBody(86090),
                       MakePollBody(0, SuccessUrl(kSessdata))};
  fake->nav_body = MakeNavBody(true);
  dialog.SetHttpClientForTest(std::move(fake));
  dialog.SetQrPollIntervalForTest(30);
  dialog.SetQrExpiryForTest(5000);
  dialog.StartQrLoginForTest();

  bool waiting = false;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (dialog.QrStateForTest() == QStringLiteral("waiting")) {
      waiting = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const bool panel = dialog.QrPanelVisibleForTest();
  const QSize image_size = dialog.QrImageSizeForTest();
  const bool image = !image_size.isEmpty();

  bool success = false;
  timer.restart();
  while (timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (dialog.QrStateForTest() == QStringLiteral("success")) {
      success = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const bool logged = dialog.AuthStateForTest() == QStringLiteral("logged_in");
  const bool credential = dialog.HasCredentialForTest();
  const bool status =
      dialog.QrStatusTextForTest().contains(QStringLiteral("已登录"));
  Check(QStringLiteral("UI smoke waiting/panel/image"),
        waiting && panel && image);
  Check(QStringLiteral("UI smoke success/logged/credential"), success && logged &&
                                                                credential &&
                                                                status);
  PrintKV("ui_smoke", waiting && panel && image && success && logged &&
                              credential && status
                          ? QStringLiteral("pass")
                          : QStringLiteral("fail"));
  dialog.CloseForTest();
  return waiting && panel && image && success && logged && credential && status;
}

int RunRealGenerate(const OnlineQrTestOptions& options) {
  lapian::online::WinHttpClient http;
  HttpCancelToken cancel;
  HttpRequest request;
  request.url = lapian::online::BuildQrGenerateUrl();
  request.timeout_ms = options.timeout_ms;
  const HttpResponse response = http.Get(request, cancel);
  PrintKV("real_generate_http", Num(response.http_status));
  PrintKV("real_cookie_sent",
          response.request_cookie_sent ? QStringLiteral("1")
                                       : QStringLiteral("0"));
  if (!response.ok) {
    PrintKV("real_generate", QStringLiteral("fail"));
    PrintKV("error", response.error);
    PrintKV("verdict", QStringLiteral("fail"));
    return 1;
  }
  const lapian::online::QrGenerateResult generated =
      lapian::online::ParseQrGenerateJson(response.body);
  PrintKV("real_generate", generated.ok ? QStringLiteral("pass")
                                        : QStringLiteral("fail"));
  PrintKV("real_qr_key_length", Num(generated.data.qrcode_key.size()));
  PrintKV("real_qr_url_https",
          generated.data.qr_url.startsWith(QStringLiteral("https://"))
              ? QStringLiteral("1")
              : QStringLiteral("0"));
  if (!generated.ok) {
    PrintKV("error", generated.error);
    PrintKV("verdict", QStringLiteral("fail"));
    return 1;
  }
  HttpRequest poll_request;
  poll_request.url = lapian::online::BuildQrPollUrl(generated.data.qrcode_key);
  poll_request.timeout_ms = options.timeout_ms;
  const HttpResponse poll_response = http.Get(poll_request, cancel);
  const lapian::online::QrPollResult polled =
      lapian::online::ParseQrPollJson(poll_response.body,
                                      poll_response.response_sessdata);
  PrintKV("real_poll_http", Num(poll_response.http_status));
  PrintKV("real_poll_data_code", Num(polled.data_code));
  PrintKV("real_poll_status",
          lapian::online::QrPollStatusName(polled.status));
  PrintKV("real_scan", QStringLiteral("not_verified"));
  const bool ok = polled.status != QrPollStatus::kFailed;
  PrintKV("verdict", ok ? QStringLiteral("pass") : QStringLiteral("fail"));
  return ok ? 0 : 1;
}

}  // namespace

int RunOnlineQrTest(const OnlineQrTestOptions& options) {
  if (options.real_generate) {
    return RunRealGenerate(options);
  }
  std::printf("T8.2.2 online_qr_test fake HTTP mode\n");
  std::fflush(stdout);
  const int failures_before = g_failures;
  bool all = true;
  all = RunRenderChecks() && all;
  all = RunSuccess(false) && all;
  all = RunSuccess(true) && all;
  all = RunNavFailure() && all;
  all = RunExpiry() && all;
  all = RunCancel() && all;
  all = RunFailures() && all;
  if (options.ui_smoke) {
    all = RunUiSmoke() && all;
  }
  PrintKV("real_scan", QStringLiteral("not_verified"));
  PrintKV("checks", Num(g_checks));
  PrintKV("failures", Num(g_failures));
  const bool pass = g_failures == failures_before && all;
  PrintKV("verdict", pass ? QStringLiteral("pass") : QStringLiteral("fail"));
  std::fflush(stdout);
  return pass ? 0 : 1;
}

}  // namespace lapian::app
