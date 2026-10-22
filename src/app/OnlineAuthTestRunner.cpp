#include "app/OnlineAuthTestRunner.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "app/MainWindow.h"
#include "app/OnlineResolveDialog.h"
#include "online/IHttpClient.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

const char kOb1ViewJson[] =
    R"json({"code":0,"message":"OK","data":{"bvid":"BV1BMnv6jE5S","aid":117476968894603,"title":"在超市后门吸烟的二人【静止系MAD】","owner":{"name":"忆雪猫"},"duration":111,"pages":[{"page":1,"cid":42915922022,"part":"在超市后门吸烟的二人【静止系MAD】","duration":111}]}})json";

class RoutingFakeHttpClient final : public lapian::online::IHttpClient {
 public:
  enum class NavMode { k101 = 0, kOk, kTimeout, kBlock };

  lapian::online::HttpResponse Get(
      const lapian::online::HttpRequest& request,
      const lapian::online::HttpCancelToken& cancel) override {
    lapian::online::HttpResponse response;
    if (request.url.contains(QStringLiteral("/x/web-interface/nav"))) {
      nav_calls.fetch_add(1);
      last_nav_had_cookie.store(!request.sessdata.isEmpty());
      response.request_cookie_sent = !request.sessdata.isEmpty();
      const NavMode mode = static_cast<NavMode>(nav_mode.load());
      if (mode == NavMode::kBlock) {
        WaitForCancel(cancel, 5000);
        if (cancel.IsCancelled()) {
          response.cancelled = true;
          response.error = QStringLiteral("请求已取消");
          return response;
        }
      }
      if (mode == NavMode::kTimeout) {
        response.timed_out = true;
        response.error = QStringLiteral("网络超时，请重试");
        return response;
      }
      response.ok = true;
      response.http_status = 200;
      response.body =
          mode == NavMode::kOk
              ? QByteArray(R"json({"code":0,"message":"OK","data":{"isLogin":true,"vipStatus":0}})json")
              : QByteArray(
                    R"json({"code":-101,"message":"账号未登录","data":{"isLogin":false}})json");
      return response;
    }

    view_calls.fetch_add(1);
    last_view_had_cookie.store(!request.sessdata.isEmpty());
    const int delay_ms = view_delay_ms.load();
    if (delay_ms > 0) {
      WaitForCancel(cancel, delay_ms);
    }
    if (cancel.IsCancelled()) {
      response.cancelled = true;
      response.error = QStringLiteral("请求已取消");
      return response;
    }
    response.ok = true;
    response.http_status = 200;
    response.body = QByteArray(kOb1ViewJson);
    return response;
  }

  std::atomic<int> nav_calls{0};
  std::atomic<int> view_calls{0};
  std::atomic<int> nav_mode{static_cast<int>(NavMode::k101)};
  std::atomic<int> view_delay_ms{0};
  std::atomic<bool> last_nav_had_cookie{false};
  std::atomic<bool> last_view_had_cookie{false};

 private:
  static void WaitForCancel(const lapian::online::HttpCancelToken& cancel,
                            int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!cancel.IsCancelled() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
};

}  // namespace

OnlineAuthTestRunner::OnlineAuthTestRunner(MainWindow* window, QObject* parent)
    : QObject(parent), window_(window) {}

void OnlineAuthTestRunner::Start() {
  QTimer::singleShot(0, this, &OnlineAuthTestRunner::Run);
}

void OnlineAuthTestRunner::Check(const QString& name, bool ok,
                                 const QString& detail) {
  ++checks_;
  if (!ok) {
    ++failures_;
  }
  std::fprintf(stderr, "T8.2 auth_test [%s] %s%s%s\n", ok ? " OK " : "FAIL",
               name.toUtf8().constData(), detail.isEmpty() ? "" : " | ",
               detail.toUtf8().constData());
  std::fflush(stderr);
}

void OnlineAuthTestRunner::Log(const QString& line) {
  std::fprintf(stderr, "T8.2 auth_test %s\n", line.toUtf8().constData());
  std::fflush(stderr);
}

void OnlineAuthTestRunner::Finish(bool ok) {
  std::fprintf(stderr, "T8.2 auth_test summary checks=%d failures=%d\n", checks_,
               failures_);
  std::fflush(stderr);
  QCoreApplication::exit(ok ? 0 : 1);
}

void OnlineAuthTestRunner::Run() {
  window_->OpenOnlineDialogForTest();
  dialog_ = window_->OnlineDialogForTest();
  Check(QStringLiteral("dialog-created"), dialog_ != nullptr);
  if (dialog_ == nullptr) {
    Finish(false);
    return;
  }

  const QString root =
      QDir::currentPath() + QStringLiteral("/build/t8.2-auth-probe");
  const QString credential_path = root + QStringLiteral("/credentials.dat");
  QFile::remove(credential_path);
  dialog_->SetCredentialPathForTest(credential_path);

  Check(QStringLiteral("privacy-visible"), dialog_->PrivacyNoticeVisibleForTest());
  const QString privacy = dialog_->PrivacyNoticeTextForTest();
  Check(QStringLiteral("privacy-text"),
        privacy.contains(QStringLiteral("DPAPI")) &&
            privacy.contains(QStringLiteral("api.bilibili.com")) &&
            privacy.contains(QStringLiteral("本地模式不联网")));
  Check(QStringLiteral("initial-anonymous"),
        dialog_->AuthStateForTest() == QStringLiteral("anonymous"));
  Check(QStringLiteral("open-no-requests"),
        dialog_->NavRequestCountForTest() == 0 &&
            dialog_->LastHttpRequestsForTest() == 0);
  // 打开对话框构造了 WinHttpClient 对象，但不应加载 winhttp.dll（O9 懒加载）。
  Check(QStringLiteral("open-no-winhttp"),
        !lapian::online::WinHttpClient::LibraryLoadedForTest());
  dialog_->HidePrivacyNoticeForTest();
  Check(QStringLiteral("privacy-hidden"), !dialog_->PrivacyNoticeVisibleForTest());

  // 非法输入：0 nav / 0 view（O1-A2 + O8-A4）。
  dialog_->SetInputForTest(QStringLiteral("BV1"));
  dialog_->StartResolveForTest();
  Check(QStringLiteral("invalid-no-view"), dialog_->LastHttpRequestsForTest() == 0);
  Check(QStringLiteral("invalid-no-nav"), dialog_->NavRequestCountForTest() == 0);
  Check(QStringLiteral("invalid-error"),
        dialog_->ErrorTextForTest().contains(QStringLiteral("12 位")));

  const QString sentinel = QStringLiteral("LAP_T82_DIALOG_SENTINEL_c3f9");
  Check(QStringLiteral("save-credential"), dialog_->SaveCredentialForTest(sentinel));
  Check(QStringLiteral("save-no-nav"), dialog_->NavRequestCountForTest() == 0);
  Check(QStringLiteral("save-not-checked"),
        dialog_->AuthStateForTest() == QStringLiteral("not_checked"));
  Check(QStringLiteral("save-status"),
        dialog_->AuthStatusTextForTest().contains(QStringLiteral("不会现在联网")));
  Check(QStringLiteral("save-status-no-secret"),
        !dialog_->AuthStatusTextForTest().contains(sentinel));
  {
    QFile file(credential_path);
    const bool opened = file.open(QIODevice::ReadOnly);
    const QByteArray bytes = opened ? file.readAll() : QByteArray();
    Check(QStringLiteral("credential-file-no-plaintext"),
          opened && !bytes.contains(sentinel.toUtf8()));
  }

  auto fake = std::make_unique<RoutingFakeHttpClient>();
  auto* fake_ptr = fake.get();
  dialog_->SetHttpClientForTest(std::move(fake));
  dialog_->SetInputForTest(QStringLiteral("BV1BMnv6jE5S"));
  dialog_->StartResolveForTest();
  Check(QStringLiteral("first-started"), dialog_->IsResolvingForTest());
  Check(QStringLiteral("first-auth-finished"), dialog_->WaitForAuthForTest(5000));
  Check(QStringLiteral("first-resolve-finished"),
        dialog_->WaitForResolveForTest(5000));
  Check(QStringLiteral("first-nav-once"), fake_ptr->nav_calls.load() == 1,
        QStringLiteral("nav=%1").arg(fake_ptr->nav_calls.load()));
  Check(QStringLiteral("first-view-once"), fake_ptr->view_calls.load() == 1,
        QStringLiteral("view=%1").arg(fake_ptr->view_calls.load()));
  Check(QStringLiteral("nav-carried-cookie"),
        fake_ptr->last_nav_had_cookie.load());
  Check(QStringLiteral("view-had-no-cookie"),
        !fake_ptr->last_view_had_cookie.load());
  Check(QStringLiteral("title"), dialog_->TitleTextForTest().contains(
                                     QStringLiteral("在超市后门")));
  Check(QStringLiteral("no-error"), dialog_->ErrorTextForTest().isEmpty(),
        dialog_->ErrorTextForTest());
  Check(QStringLiteral("auth-unavailable"),
        dialog_->AuthStateForTest() == QStringLiteral("login_unavailable"));
  Check(QStringLiteral("auth-status-unavailable"),
        dialog_->AuthStatusTextForTest().contains(QStringLiteral("登录不可用")));
  Check(QStringLiteral("quality-unavailable"),
        dialog_->QualityHintForTest().contains(QStringLiteral("登录不可用")));
  Check(QStringLiteral("view-count-not-nav"),
        dialog_->LastHttpRequestsForTest() == 1);

  // 第二次提交：本会话已检查，不再 nav（DEC-3 缓存）。
  dialog_->StartResolveForTest();
  Check(QStringLiteral("second-resolve-finished"),
        dialog_->WaitForResolveForTest(5000));
  Check(QStringLiteral("second-no-nav"), fake_ptr->nav_calls.load() == 1);
  Check(QStringLiteral("second-view"), fake_ptr->view_calls.load() == 2);

  // 保存新凭据：只回到 not_checked，不立即联网。
  Check(QStringLiteral("save-new"),
        dialog_->SaveCredentialForTest(sentinel + QStringLiteral("b")));
  Check(QStringLiteral("save-new-no-nav"), fake_ptr->nav_calls.load() == 1);
  Check(QStringLiteral("save-new-not-checked"),
        dialog_->AuthStateForTest() == QStringLiteral("not_checked"));

  // 超时：继续匿名 view，界面明确。
  fake_ptr->nav_mode.store(
      static_cast<int>(RoutingFakeHttpClient::NavMode::kTimeout));
  dialog_->StartResolveForTest();
  Check(QStringLiteral("timeout-resolve-finished"),
        dialog_->WaitForResolveForTest(5000));
  Check(QStringLiteral("timeout-auth-finished"), dialog_->WaitForAuthForTest(5000));
  Check(QStringLiteral("timeout-nav"), fake_ptr->nav_calls.load() == 2);
  Check(QStringLiteral("timeout-unavailable"),
        dialog_->AuthStateForTest() == QStringLiteral("login_unavailable"));
  Check(QStringLiteral("timeout-status"),
        dialog_->AuthStatusTextForTest().contains(QStringLiteral("超时")));
  Check(QStringLiteral("timeout-quality"),
        dialog_->QualityHintForTest().contains(QStringLiteral("登录不可用")));

  // 取消：nav 阻塞 + view 慢；取消 ≤100ms，状态回 not_checked。
  Check(QStringLiteral("save-cancel"),
        dialog_->SaveCredentialForTest(sentinel + QStringLiteral("c")));
  fake_ptr->nav_mode.store(
      static_cast<int>(RoutingFakeHttpClient::NavMode::kBlock));
  fake_ptr->view_delay_ms.store(2000);
  dialog_->StartResolveForTest();
  {
    QElapsedTimer timer;
    timer.start();
    while (!dialog_->AuthCheckingForTest() && timer.elapsed() < 1000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  Check(QStringLiteral("cancel-checking"), dialog_->AuthCheckingForTest());
  const qint64 cancel_ms = dialog_->CancelForTest();
  Check(QStringLiteral("cancel-le-100ms"), cancel_ms >= 0 && cancel_ms <= 100,
        QStringLiteral("cancel_ms=%1").arg(cancel_ms));
  Check(QStringLiteral("cancel-not-resolving"), !dialog_->IsResolvingForTest());
  Check(QStringLiteral("cancel-not-checking"), !dialog_->AuthCheckingForTest());
  Check(QStringLiteral("cancel-retryable"),
        dialog_->AuthStateForTest() == QStringLiteral("not_checked"));
  fake_ptr->view_delay_ms.store(0);

  const int nav_before_close = fake_ptr->nav_calls.load();
  dialog_->CloseForTest();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  Check(QStringLiteral("close-no-nav"),
        fake_ptr->nav_calls.load() == nav_before_close);
  Check(QStringLiteral("clear-credential"), dialog_->ClearCredentialForTest());
  Check(QStringLiteral("clear-anonymous"),
        dialog_->AuthStateForTest() == QStringLiteral("anonymous"));
  Check(QStringLiteral("clear-no-nav"),
        fake_ptr->nav_calls.load() == nav_before_close);
  Check(QStringLiteral("clear-removes-file"), !QFileInfo::exists(credential_path));
  Check(QStringLiteral("clear-status"),
        dialog_->AuthStatusTextForTest().contains(QStringLiteral("未配置凭据")));
  Check(QStringLiteral("probe-never-loaded-winhttp"),
        !lapian::online::WinHttpClient::LibraryLoadedForTest());

  dialog_->Shutdown();
  Finish(failures_ == 0);
}

}  // namespace lapian::app
