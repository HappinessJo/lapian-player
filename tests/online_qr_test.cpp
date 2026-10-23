// T8.2.2 扫码登录单测（fake HTTP；无真实账号、无网络、无 GUI）。
//
// 覆盖（对应 tasks-v2 T8.2.2 验收）：
// - 二维码本地渲染：尺寸/quiet zone/finder pattern/非法输入；
// - generate/poll JSON：qrcode_key、86101/86090/86038/0、SESSDATA 提取（URL/Cookie）；
// - 状态机：等待/已扫/过期/成功/失败/取消 + 非法迁移；
// - 控制器：1.5s 轮询可缩短、180s 过期可缩短、取消 ≤100ms、刷新丢弃旧 key；
// - 成功只保存 SESSDATA（DPAPI）且 nav 通过才 success；nav -101 不保存/不显示登录；
// - 412/429/坏 JSON 失败；错误/状态文本不含 SESSDATA/二维码 URL；
// - 真实扫码不在本测试范围（需用户手机，写“未验证”）。
//
// 退出码：0 = 全部通过；1 = 失败。

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

#include "online/CredentialStore.h"
#include "online/IHttpClient.h"
#include "online/QrCode.h"
#include "online/QrLogin.h"
#include "online/QrLoginController.h"

namespace {

using lapian::online::AuthResult;
using lapian::online::AuthState;
using lapian::online::CredentialStore;
using lapian::online::HttpCancelToken;
using lapian::online::HttpRequest;
using lapian::online::HttpResponse;
using lapian::online::IHttpClient;
using lapian::online::QrGenerateResult;
using lapian::online::QrImageResult;
using lapian::online::QrLoginController;
using lapian::online::QrLoginSession;
using lapian::online::QrLoginState;
using lapian::online::QrPollResult;
using lapian::online::QrPollStatus;

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (condition) {
    std::printf("[ OK ] %s\n", what);
  } else {
    std::printf("[FAIL] %s\n", what);
    ++g_failures;
  }
}

void CheckEqI64(qint64 actual, qint64 expected, const char* what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望：%lld\n", what,
              static_cast<long long>(actual), static_cast<long long>(expected));
  ++g_failures;
}

void CheckEq(const QString& actual, const QString& expected, const char* what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  实际：%s\n  期望：%s\n", what,
              actual.toUtf8().constData(), expected.toUtf8().constData());
  ++g_failures;
}

bool ContainsSecret(const QString& text, const QString& sentinel) {
  return !sentinel.isEmpty() && text.contains(sentinel);
}

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

QByteArray MakePollBody(int data_code, const QString& url = QString(),
                        const QString& message = QString()) {
  QJsonObject data;
  data.insert(QStringLiteral("code"), data_code);
  data.insert(QStringLiteral("message"), message);
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

// 7x7 finder：外框黑、内圈白、中心 3x3 黑。
bool FinderPatternAt(const QImage& image, int module_x, int module_y, int scale,
                     int border) {
  for (int y = 0; y < 7; ++y) {
    for (int x = 0; x < 7; ++x) {
      const bool border_cell = x == 0 || y == 0 || x == 6 || y == 6;
      const bool center_cell = x >= 2 && x <= 4 && y >= 2 && y <= 4;
      const bool expected_dark = border_cell || center_cell;
      const bool actual_dark =
          ModuleDark(image, module_x + x, module_y + y, scale, border);
      if (expected_dark != actual_dark) {
        return false;
      }
    }
  }
  return true;
}

struct FakeHttp final : IHttpClient {
  QByteArray generate_body;
  int generate_status = 200;
  QVector<QByteArray> poll_bodies;
  int poll_status = 200;
  QByteArray nav_body;
  bool nav_timed_out = false;
  bool block_poll = false;
  int poll_delay_ms = 0;
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
      if (poll_delay_ms > 0) {
        const int slices = std::max(1, poll_delay_ms / 5);
        for (int i = 0; i < slices; ++i) {
          if (cancel.IsCancelled()) {
            HttpResponse response;
            response.cancelled = true;
            response.error = QStringLiteral("cancelled");
            return response;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
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
      if (nav_timed_out) {
        HttpResponse response;
        response.timed_out = true;
        response.error = QStringLiteral("timeout");
        return response;
      }
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

void Spin(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

const QString kKey1 = QStringLiteral("0123456789abcdef0123456789abcdef");
const QString kKey2 = QStringLiteral("fedcba9876543210fedcba9876543210");
const QString kSessdata = QStringLiteral("FAKE_SESSDATA_T822_SENTINEL");

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  // ---------------------------------------------------------------- QR 渲染
  {
    const QrImageResult result = lapian::online::RenderQrImage(
        QStringLiteral("https://account.bilibili.com/h5/account-h5/auth/scan-web"),
        6, 4);
    Check(result.ok(), "qr render ok");
    Check(result.version >= 1 && result.version <= 40, "qr version range");
    Check(result.modules >= 21, "qr modules >= 21");
    const int expected_pixels = (result.modules + 8) * 6;
    CheckEqI64(result.image.width(), expected_pixels, "qr image width");
    CheckEqI64(result.image.height(), expected_pixels, "qr image height");
    Check(result.image.format() == QImage::Format_RGB32, "qr image RGB32");

    bool quiet_zone_white = true;
    for (int y = 0; y < 4 * 6; ++y) {
      for (int x = 0; x < 4 * 6; ++x) {
        const QRgb pixel = result.image.pixel(x, y);
        if (qRed(pixel) != 255 || qGreen(pixel) != 255 ||
            qBlue(pixel) != 255) {
          quiet_zone_white = false;
        }
      }
    }
    Check(quiet_zone_white, "qr quiet zone white");
    Check(FinderPatternAt(result.image, 0, 0, 6, 4),
          "qr top-left finder");
    Check(FinderPatternAt(result.image, result.modules - 7, 0, 6, 4),
          "qr top-right finder");
    Check(FinderPatternAt(result.image, 0, result.modules - 7, 6, 4),
          "qr bottom-left finder");
    Check(!lapian::online::RenderQrImage(QString(), 6, 4).ok(),
          "qr empty text rejected");
    Check(!lapian::online::RenderQrImage(QString(5000, QLatin1Char('a')), 6, 4)
               .ok(),
          "qr too long rejected");
    Check(lapian::online::RenderQrImage(QStringLiteral("x"), 1, 0).module_pixels ==
              2,
          "qr module scale clamped");
  }

  // ---------------------------------------------------------------- generate 解析
  {
    const QByteArray body = MakeGenerateBody(kKey1);
    const QrGenerateResult result = lapian::online::ParseQrGenerateJson(body);
    Check(result.ok, "generate parse ok");
    CheckEq(result.data.qrcode_key, kKey1, "generate key");
    Check(result.data.qr_url.startsWith(QStringLiteral("https://")),
          "generate url https");

    Check(!lapian::online::ParseQrGenerateJson("{}").ok,
          "generate empty rejected");
    Check(!lapian::online::ParseQrGenerateJson("{bad").ok,
          "generate bad json rejected");
    const QByteArray bad_key = MakeGenerateBody(QStringLiteral("bad/key"));
    Check(!lapian::online::ParseQrGenerateJson(bad_key).ok,
          "generate unsafe key rejected");
    const QByteArray bad_url = R"({"code":0,"data":{"url":"http://x/y","qrcode_key":"0123456789abcdef0123456789abcdef"}})";
    Check(!lapian::online::ParseQrGenerateJson(bad_url).ok,
          "generate http url rejected");
    Check(lapian::online::BuildQrPollUrl(QStringLiteral("../evil")).isEmpty(),
          "poll url rejects unsafe key");
    Check(lapian::online::BuildQrPollUrl(kKey1).contains(
              QStringLiteral("qrcode_key=") + kKey1),
          "poll url contains safe key");
  }

  // ---------------------------------------------------------------- poll 解析
  {
    const QrPollResult waiting = lapian::online::ParseQrPollJson(MakePollBody(86101));
    Check(waiting.status == QrPollStatus::kWaiting, "poll 86101 waiting");
    const QrPollResult scanned = lapian::online::ParseQrPollJson(MakePollBody(86090));
    Check(scanned.status == QrPollStatus::kScanned, "poll 86090 scanned");
    const QrPollResult expired = lapian::online::ParseQrPollJson(MakePollBody(86038));
    Check(expired.status == QrPollStatus::kExpired, "poll 86038 expired");
    const QrPollResult unknown = lapian::online::ParseQrPollJson(MakePollBody(12345));
    Check(unknown.status == QrPollStatus::kFailed, "poll unknown failed");
    Check(!unknown.error.contains(QStringLiteral("http")),
          "poll unknown error has no url");

    const QrPollResult success = lapian::online::ParseQrPollJson(
        MakePollBody(0, SuccessUrl(kSessdata)));
    Check(success.status == QrPollStatus::kSuccess, "poll success");
    CheckEq(success.sessdata, kSessdata, "poll success sessdata");
    Check(!ContainsSecret(success.message, kSessdata),
          "poll success message no sessdata");

    const QString encoded = QStringLiteral("FAKE%2CSESSDATA%2FENCODED");
    const QrPollResult encoded_success = lapian::online::ParseQrPollJson(
        MakePollBody(0, SuccessUrl(encoded)));
    Check(encoded_success.status == QrPollStatus::kSuccess,
          "poll encoded sessdata success");
    CheckEq(encoded_success.sessdata, encoded,
            "poll encoded sessdata keeps percent escapes");

    const QrPollResult cookie_success = lapian::online::ParseQrPollJson(
        MakePollBody(0, QString()), kSessdata);
    Check(cookie_success.status == QrPollStatus::kSuccess,
          "poll cookie success");
    CheckEq(cookie_success.sessdata, kSessdata, "poll cookie sessdata");

    const QrPollResult no_secret =
        lapian::online::ParseQrPollJson(MakePollBody(0, QString()));
    Check(no_secret.status == QrPollStatus::kFailed,
          "poll success without sessdata failed");
    Check(lapian::online::ParseQrPollJson("{bad").status !=
              QrPollStatus::kSuccess,
          "poll bad json not success");
    Check(lapian::online::ParseQrPollJson(
              R"({"code":-101,"message":"not login"})")
              .status == QrPollStatus::kFailed,
          "poll api -101 failed");
  }

  // ---------------------------------------------------------------- 状态机
  {
    QrLoginSession session;
    Check(session.state() == QrLoginState::kIdle, "state idle");
    Check(!session.OnPolled(lapian::online::ParseQrPollJson(MakePollBody(86101))),
          "state idle ignores poll");
    Check(session.Start(), "state start");
    Check(session.state() == QrLoginState::kGenerating, "state generating");
    Check(!session.Start(), "state generating rejects second start");
    Check(session.OnGenerated(lapian::online::ParseQrGenerateJson(
              MakeGenerateBody(kKey1))),
          "state generated accepted");
    Check(session.state() == QrLoginState::kWaiting, "state waiting");
    CheckEq(session.qrcodeKey(), kKey1, "state key");
    Check(session.OnPolled(lapian::online::ParseQrPollJson(MakePollBody(86101))),
          "state waiting poll accepted");
    Check(session.OnPolled(lapian::online::ParseQrPollJson(MakePollBody(86090))),
          "state scanned poll accepted");
    Check(session.state() == QrLoginState::kScanned, "state scanned");
    Check(session.OnPolled(lapian::online::ParseQrPollJson(
              MakePollBody(0, SuccessUrl(kSessdata)))),
          "state success accepted");
    Check(session.state() == QrLoginState::kVerifying, "state verifying");
    CheckEq(session.sessdata(), kSessdata, "state sessdata");
    Check(session.OnVerified(true, QString()),
          "state nav success accepted");
    Check(!session.OnVerified(true, QString()),
          "state verifying rejects double verify");
    Check(session.state() == QrLoginState::kSuccess, "state success");
    Check(!session.Cancel(), "state success cancel no-op");
    session.ClearSessdata();
    Check(session.sessdata().isEmpty(), "state sessdata cleared");
  }

  {
    QrLoginSession session;
    session.Start();
    session.OnGenerated(lapian::online::ParseQrGenerateJson(MakeGenerateBody(kKey1)));
    session.OnPolled(lapian::online::ParseQrPollJson(MakePollBody(86038)));
    Check(session.state() == QrLoginState::kExpired, "state expired");
    Check(session.qrcodeKey().isEmpty(), "state expired clears key");
    Check(session.Start(), "state expired can restart");
    Check(session.state() == QrLoginState::kGenerating, "state restart generating");
    session.OnGenerated(lapian::online::ParseQrGenerateJson(MakeGenerateBody(kKey2)));
    CheckEq(session.qrcodeKey(), kKey2, "state new key");
    Check(session.Cancel(), "state cancel from waiting");
    Check(session.state() == QrLoginState::kCancelled, "state cancelled");
    Check(!session.OnPolled(lapian::online::ParseQrPollJson(MakePollBody(86101))),
          "state cancelled ignores poll");
    Check(session.qrcodeKey().isEmpty(), "state cancelled clears key");
  }

  {
    QrLoginSession session;
    session.Start();
    session.OnGenerated(lapian::online::ParseQrGenerateJson(MakeGenerateBody(kKey1)));
    session.OnPolled(lapian::online::ParseQrPollJson(
        MakePollBody(0, SuccessUrl(kSessdata))));
    Check(session.OnVerified(false, QStringLiteral("nav -101")),
          "state nav failure accepted");
    Check(session.state() == QrLoginState::kFailed, "state nav failed");
    Check(session.sessdata().isEmpty(), "state nav failed clears sessdata");
  }

  // ---------------------------------------------------------------- 控制器：成功
  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {MakePollBody(86101), MakePollBody(86090),
                        MakePollBody(0, SuccessUrl(kSessdata))};
    http.nav_body = MakeNavBody(true);

    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(40);
    controller.SetExpiryMsForTest(5000);
    controller.SetNavTimeoutForTest(1000);
    int success_signals = 0;
    int failure_signals = 0;
    QObject::connect(&controller, &QrLoginController::loginSucceeded,
                     [&success_signals]() { ++success_signals; });
    QObject::connect(&controller, &QrLoginController::loginFailed,
                     [&failure_signals](const QString&) { ++failure_signals; });
    QImage image;
    QObject::connect(&controller, &QrLoginController::qrImageChanged,
                     [&image](const QImage& value) { image = value; });

    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kWaiting, 2000),
          "controller waiting");
    Check(!image.isNull(), "controller qr image");
    Check(controller.qrContentForTest().startsWith(QStringLiteral("https://")),
          "controller qr content");
    Check(WaitForState(&controller, QrLoginState::kSuccess, 5000),
          "controller success");
    CheckEqI64(success_signals, 1, "controller success signal");
    CheckEqI64(failure_signals, 0, "controller no failure signal");
    Check(controller.authResult().ok(), "controller auth result");
    Check(controller.authResult().state == AuthState::kLoggedIn,
          "controller auth logged in");
    CheckEqI64(http.generate_requests, 1, "controller one generate");
    CheckEqI64(http.poll_requests, 3, "controller three polls");
    CheckEqI64(http.nav_requests, 1, "controller one nav");
    lapian::online::CredentialLoadResult loaded = credentials.Load();
    Check(loaded.has_credential, "controller credential saved");
    CheckEq(loaded.sessdata, kSessdata, "controller only sessdata saved");
    Check(!ContainsSecret(controller.message(), kSessdata),
          "controller message no sessdata");
    Check(!ContainsSecret(controller.qrContentForTest(), kSessdata),
          "controller qr content no sessdata");
    for (const HttpRequest& request : http.requests) {
      if (QUrl(request.url).path().contains(QStringLiteral("qrcode"))) {
        Check(request.sessdata.isEmpty(), "controller qr request has no cookie");
      }
    }
    controller.Cancel();
    Check(controller.state() == QrLoginState::kSuccess,
          "controller cancel after success keeps login");
    const lapian::online::CredentialLoadResult still = credentials.Load();
    CheckEq(still.sessdata, kSessdata, "controller success credential kept");
    lapian::online::ClearSecret(&loaded.sessdata);
  }

  // ---------------------------------------------------------------- 控制器：cookie-only + nav -101
  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {MakePollBody(0, QString())};
    http.cookie_sessdata = kSessdata;
    http.nav_body = MakeNavBody(false, -101);

    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(30);
    controller.SetExpiryMsForTest(5000);
    controller.SetNavTimeoutForTest(1000);
    int success_signals = 0;
    int failure_signals = 0;
    QObject::connect(&controller, &QrLoginController::loginSucceeded,
                     [&success_signals]() { ++success_signals; });
    QObject::connect(&controller, &QrLoginController::loginFailed,
                     [&failure_signals](const QString&) { ++failure_signals; });
    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kFailed, 5000),
          "controller nav -101 failed");
    CheckEqI64(success_signals, 0, "controller nav -101 no success");
    Check(failure_signals >= 1, "controller nav -101 failure signal");
    Check(!credentials.Load().has_credential,
          "controller nav -101 does not save credential");
    Check(!controller.authResult().ok(), "controller nav -101 no login");
  }

  // ---------------------------------------------------------------- 控制器：过期 + 刷新
  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {MakePollBody(86101)};
    http.nav_body = MakeNavBody(true);

    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(30);
    controller.SetExpiryMsForTest(250);
    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kExpired, 5000),
          "controller expiry");
    const int polls_at_expiry = http.poll_requests;
    Spin(300);
    CheckEqI64(http.poll_requests, polls_at_expiry,
               "controller no poll after expiry");
    Check(controller.qrKeyForTest().isEmpty(), "controller expired clears key");
    Check(controller.qrImage().isNull(), "controller expired clears image");

    http.generate_body = MakeGenerateBody(kKey2);
    controller.Refresh();
    Check(WaitForState(&controller, QrLoginState::kWaiting, 3000),
          "controller refresh waiting");
    CheckEq(controller.qrKeyForTest(), kKey2, "controller refresh new key");
    CheckEqI64(http.generate_requests, 2, "controller refresh regenerate");
    controller.Cancel();
  }

  // ---------------------------------------------------------------- 控制器：取消 ≤100ms
  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {MakePollBody(86101)};
    http.block_poll = true;
    http.nav_body = MakeNavBody(true);

    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(30);
    controller.SetExpiryMsForTest(5000);
    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kWaiting, 3000),
          "controller cancel waiting");
    Spin(100);  // 让第一次 poll 进入阻塞
    QElapsedTimer timer;
    timer.start();
    controller.Cancel();
    const qint64 cancel_ms = timer.elapsed();
    Check(controller.state() == QrLoginState::kCancelled,
          "controller cancelled state");
    Check(cancel_ms <= 100, "controller cancel <=100ms");
    const int polls_after_cancel = http.poll_requests;
    Spin(200);
    CheckEqI64(http.poll_requests, polls_after_cancel,
               "controller no poll after cancel");
  }

  // ---------------------------------------------------------------- 控制器：412 / 429 / 坏 JSON
  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.generate_status = 412;
    QrLoginController controller(&http, &credentials);
    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kFailed, 3000),
          "controller generate 412 failed");
    controller.Refresh();
    Check(WaitForState(&controller, QrLoginState::kFailed, 3000),
          "controller refresh after 412 failed");
    Check(http.generate_requests >= 1, "controller 412 attempted");
  }

  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {MakePollBody(86101)};
    http.poll_status = 429;
    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(30);
    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kFailed, 4000),
          "controller poll 429 failed");
    CheckEqI64(http.poll_requests, 1, "controller 429 no retry loop");
  }

  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = QByteArray("{bad json");
    QrLoginController controller(&http, &credentials);
    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kFailed, 3000),
          "controller bad generate json failed");
    Check(!ContainsSecret(controller.message(), kSessdata),
          "controller bad json message no secret");
  }

  {
    QTemporaryDir temp;
    CredentialStore credentials(QDir(temp.path()).filePath(
        QStringLiteral("credentials.dat")));
    FakeHttp http;
    http.generate_body = MakeGenerateBody(kKey1);
    http.poll_bodies = {QByteArray("{bad poll")};
    QrLoginController controller(&http, &credentials);
    controller.SetPollIntervalForTest(30);
    controller.Start();
    Check(WaitForState(&controller, QrLoginState::kFailed, 4000),
          "controller bad poll json failed");
  }

  // ---------------------------------------------------------------- 常量
  CheckEqI64(lapian::online::kQrLoginExpiryMs, 180000, "qr expiry 180s");
  CheckEqI64(lapian::online::kQrPollIntervalMs, 1500, "qr poll 1.5s");

  std::printf("checks=%d failures=%d\n", g_checks, g_failures);
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}
