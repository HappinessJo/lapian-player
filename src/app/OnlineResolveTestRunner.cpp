#include "app/OnlineResolveTestRunner.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "app/MainWindow.h"
#include "app/OnlineResolveDialog.h"
#include "online/IHttpClient.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

constexpr int kLiveTimeoutMs = 9000;

// fake 慢客户端只用于取消/关窗时序；返回体不是测试目标（真实样本走 WinHTTP）。
const char kFakeViewJson[] =
    R"json({"code":0,"message":"OK","data":{"bvid":"BV1BMnv6jE5S","aid":1,"title":"fake","owner":{"name":"fake"},"duration":111,"pages":[{"page":1,"cid":42915922022,"part":"fake","duration":111}]}})json";

class SlowFakeHttpClient final : public lapian::online::IHttpClient {
 public:
  explicit SlowFakeHttpClient(int delay_ms) : delay_ms_(delay_ms) {}

  lapian::online::HttpResponse Get(
      const lapian::online::HttpRequest&,
      const lapian::online::HttpCancelToken& cancel) override {
    requests_.fetch_add(1);
    lapian::online::HttpResponse response;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < delay_ms_) {
      if (cancel.IsCancelled()) {
        response.cancelled = true;
        response.error = QStringLiteral("请求已取消");
        return response;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    response.ok = true;
    response.http_status = 200;
    response.body = QByteArray(kFakeViewJson);
    return response;
  }

  int requests() const { return requests_.load(); }

 private:
  int delay_ms_ = 0;
  std::atomic<int> requests_{0};
};

}  // namespace

OnlineResolveTestRunner::OnlineResolveTestRunner(MainWindow* window, QObject* parent)
    : QObject(parent), window_(window) {}

void OnlineResolveTestRunner::Start() {
  Enqueue(QStringLiteral("dialog-basics"), [this]() {
    StepDialogBasics();
    RunNext();
  });
  Enqueue(QStringLiteral("local-invalid"), [this]() {
    StepLocalInvalid();
    RunNext();
  });
  Enqueue(QStringLiteral("live-ob1"), [this]() {
    StepLiveSuccess(QStringLiteral("ob1"), QStringLiteral("BV1BMnv6jE5S"),
                    QStringLiteral("在超市后门吸烟的二人"),
                    QStringLiteral("忆雪猫"), QStringLiteral("01:51"),
                    42915922022LL);
  });
  Enqueue(QStringLiteral("live-ob2"), [this]() {
    StepLiveSuccess(QStringLiteral("ob2"), QStringLiteral("BV1mA411m7pS"),
                    QStringLiteral("よふかしのうた"), QStringLiteral("benzun_"),
                    QStringLiteral("01:36"), 1004967230LL);
  });
  Enqueue(QStringLiteral("live-ob3"), [this]() {
    StepLiveSuccess(QStringLiteral("ob3"), QStringLiteral("BV1GJ411x7h7"),
                    QStringLiteral("Never Gonna Give You Up"),
                    QStringLiteral("索尼音乐中国"), QStringLiteral("03:33"),
                    137649199LL);
  });
  Enqueue(QStringLiteral("live-url-p1"), [this]() {
    StepLiveUrl();
  });
  Enqueue(QStringLiteral("live-multip"), [this]() {
    StepMultiPage();
  });
  Enqueue(QStringLiteral("failure-400"), [this]() {
    StepFailureCode(QStringLiteral("failure-400"), QStringLiteral("BV0000000000"),
                    QStringLiteral("参数错误"));
  });
  Enqueue(QStringLiteral("failure-404"), [this]() {
    StepFailureCode(QStringLiteral("failure-404"), QStringLiteral("BV1zz411c7mD"),
                    QStringLiteral("不存在"));
  });
  Enqueue(QStringLiteral("failure-62002"), [this]() {
    StepFailureCode(QStringLiteral("failure-62002"), QStringLiteral("BV1Q541167Qh"),
                    QStringLiteral("不可见"));
  });
  Enqueue(QStringLiteral("mismatch-confirm"), [this]() {
    StepMismatch();
  });
  Enqueue(QStringLiteral("cancel-fake"), [this]() {
    StepCancelFake();
  });
  Enqueue(QStringLiteral("close-fake"), [this]() {
    StepCloseFake();
  });
  Enqueue(QStringLiteral("done"), [this]() { Finish(); });
  RunNext();
}

void OnlineResolveTestRunner::Enqueue(const QString& name, Step step) {
  steps_.emplace_back(name, std::move(step));
}

void OnlineResolveTestRunner::RunNext() {
  if (steps_.empty()) {
    Finish();
    return;
  }
  auto item = std::move(steps_.front());
  steps_.pop_front();
  QTimer::singleShot(0, this, [this, name = item.first, step = item.second]() {
    Log(QStringLiteral("step ") + name);
    step();
  });
}

void OnlineResolveTestRunner::Check(const QString& name, bool ok,
                                    const QString& detail) {
  ++checks_;
  if (ok) {
    std::fprintf(stderr, "T8.1 online_test [ OK ] %s\n",
                 name.toUtf8().constData());
  } else {
    ++failures_;
    std::fprintf(stderr, "T8.1 online_test [FAIL] %s%s%s\n",
                 name.toUtf8().constData(), detail.isEmpty() ? "" : " | ",
                 detail.toUtf8().constData());
  }
  std::fflush(stderr);
}

void OnlineResolveTestRunner::Log(const QString& line) {
  std::fprintf(stderr, "T8.1 online_test %s\n", line.toUtf8().constData());
  std::fflush(stderr);
}

void OnlineResolveTestRunner::Finish() {
  Log(QStringLiteral("summary checks=%1 failures=%2 max_local_invalid_ms=%3 "
                     "confirmed_events=%4")
          .arg(checks_)
          .arg(failures_)
          .arg(max_local_invalid_ms_)
          .arg(confirmed_events_));
  if (dialog_ != nullptr) {
    dialog_->CloseForTest();
  }
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

void OnlineResolveTestRunner::PollUntil(const QString& what, int timeout_ms,
                                        std::function<bool()> predicate,
                                        Step on_ready, Step on_timeout) {
  auto timer = std::make_shared<QElapsedTimer>();
  timer->start();
  auto poll = std::make_shared<std::function<void()>>();
  *poll = [this, what, timeout_ms, timer, predicate, on_ready, on_timeout, poll]() {
    if (predicate()) {
      if (on_ready) {
        on_ready();
      }
      RunNext();
      return;
    }
    if (timer->elapsed() > timeout_ms) {
      Check(what + QStringLiteral("-timeout"), false,
            QStringLiteral("elapsed_ms=%1").arg(timer->elapsed()));
      if (on_timeout) {
        on_timeout();
      }
      RunNext();
      return;
    }
    QTimer::singleShot(20, this, [poll]() { (*poll)(); });
  };
  QTimer::singleShot(0, this, [poll]() { (*poll)(); });
}

void OnlineResolveTestRunner::SaveShot(const QString& name) {
  if (dialog_ == nullptr) {
    Check(QStringLiteral("shot-") + name, false, QStringLiteral("no dialog"));
    return;
  }
  const QString dir = QDir::currentPath() + QStringLiteral("/build");
  QDir().mkpath(dir);
  const QString path =
      dir + QStringLiteral("/t8.1-online-resolve-") + name + QStringLiteral(".png");
  const bool ok = dialog_->grab().save(path);
  Check(QStringLiteral("shot-") + name, ok && QFileInfo(path).size() > 0, path);
  Log(QStringLiteral("shot path=%1 bytes=%2")
          .arg(path)
          .arg(QFileInfo(path).size()));
}

void OnlineResolveTestRunner::StepDialogBasics() {
  window_->OpenOnlineDialogForTest();
  dialog_ = window_->OnlineDialogForTest();
  Check(QStringLiteral("dialog-created"), dialog_ != nullptr);
  if (dialog_ == nullptr) {
    Finish();
    return;
  }

  // T8.2：T8.1 探针必须保持匿名（156 项、每次解析只发 1 个 view 请求）。
  // 指向不存在的临时凭据路径，避免读到本机真实 SESSDATA 后多发 nav。
  const QString probe_credential =
      QDir::currentPath() + QStringLiteral("/build/t8.2-probe/credentials.dat");
  QFile::remove(probe_credential);
  dialog_->SetCredentialPathForTest(probe_credential);
  Check(QStringLiteral("dialog-visible"), dialog_->isVisible());
  Check(QStringLiteral("shortcut-ctrl-u"),
        window_->ShortcutMapForTest().contains(QStringLiteral("online=Ctrl+U")),
        window_->ShortcutMapForTest());
  Check(QStringLiteral("initial-confirm-disabled"),
        !dialog_->ConfirmEnabledForTest());
  Check(QStringLiteral("initial-page-count-zero"), dialog_->PageCountForTest() == 0);
  Check(QStringLiteral("initial-no-error"), dialog_->ErrorTextForTest().isEmpty());
  Check(QStringLiteral("winhttp-not-loaded-before-resolve"),
        !lapian::online::WinHttpClient::LibraryLoadedForTest());
  QObject::connect(dialog_, &OnlineResolveDialog::confirmed, this,
                   [this](const QString& bvid, qint64 cid, int page) {
                     ++confirmed_events_;
                     last_confirmed_bvid_ = bvid;
                     last_confirmed_cid_ = cid;
                     last_confirmed_page_ = page;
                   });
}

void OnlineResolveTestRunner::StepLocalInvalid() {
  if (dialog_ == nullptr) {
    return;
  }
  const std::vector<std::pair<QString, QString>> cases = {
      {QString(), QStringLiteral("请输入")},
      {QStringLiteral("BV1"), QStringLiteral("12 位")},
      {QStringLiteral("BV1BMnv6jE5"), QStringLiteral("12 位")},
      {QStringLiteral("BV1BMnv6jE5!"), QStringLiteral("非法字符")},
      {QStringLiteral("av170001"), QStringLiteral("av")},
      {QStringLiteral("b23.tv/abc"), QStringLiteral("短链")},
      {QStringLiteral("https://example.com/video/BV1BMnv6jE5S"),
       QStringLiteral("bilibili.com")},
      {QStringLiteral("https://www.bilibili.com/video/BV1BMnv6jE5S?p=0"),
       QStringLiteral("正整数")},
      {QStringLiteral("随便搜索一个视频"), QStringLiteral("搜索词")},
  };

  for (int i = 0; i < static_cast<int>(cases.size()); ++i) {
    dialog_->SetHttpClientForTest(nullptr);
    dialog_->SetInputForTest(cases[i].first);
    QElapsedTimer timer;
    timer.start();
    dialog_->StartResolveForTest();
    const qint64 elapsed_ms = timer.elapsed();
    max_local_invalid_ms_ = std::max(max_local_invalid_ms_, elapsed_ms);
    const QString prefix = QStringLiteral("local-%1").arg(i);
    Check(prefix + QStringLiteral("-fast"), elapsed_ms <= 100,
          QStringLiteral("elapsed_ms=%1 input=%2")
              .arg(elapsed_ms)
              .arg(cases[i].first));
    Check(prefix + QStringLiteral("-no-thread"), !dialog_->IsResolvingForTest());
    Check(prefix + QStringLiteral("-no-request"),
          dialog_->LastHttpRequestsForTest() == 0 &&
              dialog_->TotalHttpRequestsForTest() == 0,
          QStringLiteral("last=%1 total=%2")
              .arg(dialog_->LastHttpRequestsForTest())
              .arg(dialog_->TotalHttpRequestsForTest()));
    Check(prefix + QStringLiteral("-error"),
          dialog_->ErrorTextForTest().contains(cases[i].second),
          dialog_->ErrorTextForTest());
    Check(prefix + QStringLiteral("-confirm-off"),
          !dialog_->ConfirmEnabledForTest());
    Log(QStringLiteral("local_invalid input=%1 elapsed_ms=%2 requests=0 error=%3")
            .arg(cases[i].first)
            .arg(elapsed_ms)
            .arg(dialog_->ErrorTextForTest()));
  }
  Check(QStringLiteral("local-max-le-100ms"), max_local_invalid_ms_ <= 100,
        QStringLiteral("max_ms=%1").arg(max_local_invalid_ms_));
}

void OnlineResolveTestRunner::StepLiveSuccess(const QString& label,
                                              const QString& input,
                                              const QString& expected_title,
                                              const QString& expected_owner,
                                              const QString& expected_duration,
                                              qint64 expected_cid) {
  if (dialog_ == nullptr) {
    return;
  }
  dialog_->SetHttpClientForTest(nullptr);
  dialog_->SetInputForTest(input);
  dialog_->StartResolveForTest();
  Check(label + QStringLiteral("-started"), dialog_->IsResolvingForTest());
  PollUntil(
      label, kLiveTimeoutMs, [this]() { return !dialog_->IsResolvingForTest(); },
      [this, label, expected_title, expected_owner, expected_duration,
       expected_cid]() {
        Check(label + QStringLiteral("-requests"),
              dialog_->LastHttpRequestsForTest() == 1,
              QStringLiteral("last=%1")
                  .arg(dialog_->LastHttpRequestsForTest()));
        Check(label + QStringLiteral("-no-error"),
              dialog_->ErrorTextForTest().isEmpty(), dialog_->ErrorTextForTest());
        Check(label + QStringLiteral("-title"),
              dialog_->TitleTextForTest().contains(expected_title),
              dialog_->TitleTextForTest());
        Check(label + QStringLiteral("-owner"),
              dialog_->OwnerTextForTest().contains(expected_owner),
              dialog_->OwnerTextForTest());
        Check(label + QStringLiteral("-total-duration"),
              dialog_->TotalDurationTextForTest() == expected_duration,
              dialog_->TotalDurationTextForTest());
        Check(label + QStringLiteral("-page-duration"),
              dialog_->DurationTextForTest() == expected_duration,
              dialog_->DurationTextForTest());
        Check(label + QStringLiteral("-page-count"),
              dialog_->PageCountForTest() == 1,
              QStringLiteral("count=%1").arg(dialog_->PageCountForTest()));
        Check(label + QStringLiteral("-selected-p1"),
              dialog_->SelectedPageIndexForTest() == 0);
        Check(label + QStringLiteral("-cid"),
              dialog_->CurrentPageTextForTest().contains(
                  QString::number(expected_cid)),
              dialog_->CurrentPageTextForTest());
        Check(label + QStringLiteral("-quality-hint"),
              dialog_->QualityHintForTest().contains(QStringLiteral("清晰度受限")) &&
                  !dialog_->QualityHintForTest().contains(QStringLiteral("720")) &&
                  !dialog_->QualityHintForTest().contains(QStringLiteral("1080")) &&
                  !dialog_->QualityHintForTest().contains(QStringLiteral("480")),
              dialog_->QualityHintForTest());
        Check(label + QStringLiteral("-confirm-enabled"),
              dialog_->ConfirmEnabledForTest());
        Check(label + QStringLiteral("-not-confirmed"),
              !dialog_->ConfirmedForTest());
        Check(label + QStringLiteral("-no-playback"), !window_->HasMediaForTest());
        if (label == QStringLiteral("ob1")) {
          ob1_title_ = dialog_->TitleTextForTest();
          Check(QStringLiteral("winhttp-loaded-after-first-resolve"),
                lapian::online::WinHttpClient::LibraryLoadedForTest());
          SaveShot(QStringLiteral("ob1"));
        } else if (label == QStringLiteral("ob2")) {
          ob2_title_ = dialog_->TitleTextForTest();
          Check(QStringLiteral("ob2-title-differs"), ob2_title_ != ob1_title_);
        } else if (label == QStringLiteral("ob3")) {
          Check(QStringLiteral("ob3-title-differs"),
                dialog_->TitleTextForTest() != ob1_title_ &&
                    dialog_->TitleTextForTest() != ob2_title_);
        }
        Log(QStringLiteral("%1 title=%2 owner=%3 duration=%4 cid=%5")
                .arg(label)
                .arg(dialog_->TitleTextForTest())
                .arg(dialog_->OwnerTextForTest())
                .arg(dialog_->TotalDurationTextForTest())
                .arg(dialog_->CurrentPageTextForTest()));
      },
      [this]() { dialog_->CancelForTest(); });
}

void OnlineResolveTestRunner::StepLiveUrl() {
  if (dialog_ == nullptr) {
    return;
  }
  dialog_->SetInputForTest(
      QStringLiteral("https://www.bilibili.com/video/BV1BMnv6jE5S?p=1&spm_id_from=333.999"));
  dialog_->StartResolveForTest();
  PollUntil(
      QStringLiteral("url-p1"), kLiveTimeoutMs,
      [this]() { return !dialog_->IsResolvingForTest(); },
      [this]() {
        Check(QStringLiteral("url-p1-no-error"),
              dialog_->ErrorTextForTest().isEmpty(), dialog_->ErrorTextForTest());
        Check(QStringLiteral("url-p1-title"),
              dialog_->TitleTextForTest() == ob1_title_,
              dialog_->TitleTextForTest());
        Check(QStringLiteral("url-p1-page-1"),
              dialog_->SelectedPageIndexForTest() == 0 &&
                  dialog_->CurrentPageTextForTest().contains(
                      QStringLiteral("42915922022")),
              dialog_->CurrentPageTextForTest());
        Check(QStringLiteral("url-p1-requests"),
              dialog_->LastHttpRequestsForTest() == 1);
        Log(QStringLiteral("url_p1 title=%1 current=%2")
                .arg(dialog_->TitleTextForTest())
                .arg(dialog_->CurrentPageTextForTest()));
      },
      [this]() { dialog_->CancelForTest(); });
}

void OnlineResolveTestRunner::StepMultiPage() {
  if (dialog_ == nullptr) {
    return;
  }
  dialog_->SetInputForTest(
      QStringLiteral("https://www.bilibili.com/video/BV1fNnv6EEon?p=2"));
  dialog_->StartResolveForTest();
  PollUntil(
      QStringLiteral("multip"), kLiveTimeoutMs,
      [this]() { return !dialog_->IsResolvingForTest(); },
      [this]() {
        Check(QStringLiteral("multip-no-error"),
              dialog_->ErrorTextForTest().isEmpty(), dialog_->ErrorTextForTest());
        Check(QStringLiteral("multip-title"),
              dialog_->TitleTextForTest().contains(QStringLiteral("代号：神不言")),
              dialog_->TitleTextForTest());
        Check(QStringLiteral("multip-page-count"),
              dialog_->PageCountForTest() == 6,
              QStringLiteral("count=%1").arg(dialog_->PageCountForTest()));
        Check(QStringLiteral("multip-selected-p2-index"),
              dialog_->SelectedPageIndexForTest() == 1);
        Check(QStringLiteral("multip-total-duration"),
              dialog_->TotalDurationTextForTest() == QStringLiteral("08:21"),
              dialog_->TotalDurationTextForTest());
        Check(QStringLiteral("multip-p2-duration"),
              dialog_->DurationTextForTest() == QStringLiteral("00:57"),
              dialog_->DurationTextForTest());
        Check(QStringLiteral("multip-p2-cid"),
              dialog_->CurrentPageTextForTest().contains(QStringLiteral("P2")) &&
                  dialog_->CurrentPageTextForTest().contains(
                      QStringLiteral("42915138952")),
              dialog_->CurrentPageTextForTest());
        Check(QStringLiteral("multip-confirm-enabled"),
              dialog_->ConfirmEnabledForTest());
        SaveShot(QStringLiteral("multip"));

        dialog_->SelectPageForTest(0);
        Check(QStringLiteral("multip-switch-p1"),
              dialog_->SelectedPageIndexForTest() == 0 &&
                  dialog_->DurationTextForTest() == QStringLiteral("01:50") &&
                  dialog_->CurrentPageTextForTest().contains(
                      QStringLiteral("42915136857")),
              dialog_->CurrentPageTextForTest() + QStringLiteral(" / ") +
                  dialog_->DurationTextForTest());
        Check(QStringLiteral("multip-p1-reconfirm"),
              !dialog_->ConfirmedForTest() && dialog_->ConfirmEnabledForTest());

        dialog_->SelectPageForTest(2);
        Check(QStringLiteral("multip-switch-p3"),
              dialog_->SelectedPageIndexForTest() == 2 &&
                  dialog_->DurationTextForTest() == QStringLiteral("01:50") &&
                  dialog_->CurrentPageTextForTest().contains(
                      QStringLiteral("42915201367")),
              dialog_->CurrentPageTextForTest() + QStringLiteral(" / ") +
                  dialog_->DurationTextForTest());

        dialog_->ClickConfirmForTest();
        Check(QStringLiteral("multip-confirmed"), dialog_->ConfirmedForTest());
        Check(QStringLiteral("multip-confirm-event"),
              confirmed_events_ == 1 && last_confirmed_page_ == 3 &&
                  last_confirmed_cid_ == 42915201367LL &&
                  last_confirmed_bvid_ == QStringLiteral("BV1fNnv6EEon"),
              QStringLiteral("events=%1 page=%2 cid=%3")
                  .arg(confirmed_events_)
                  .arg(last_confirmed_page_)
                  .arg(last_confirmed_cid_));
        Log(QStringLiteral("multip pages=6 p2_cid=42915138952 p1_cid=42915136857 "
                           "p3_cid=42915201367"));
      },
      [this]() { dialog_->CancelForTest(); });
}

void OnlineResolveTestRunner::StepFailureCode(const QString& label,
                                              const QString& input,
                                              const QString& expected_term) {
  if (dialog_ == nullptr) {
    return;
  }
  dialog_->SetHttpClientForTest(nullptr);
  dialog_->SetInputForTest(input);
  dialog_->StartResolveForTest();
  PollUntil(
      label, kLiveTimeoutMs, [this]() { return !dialog_->IsResolvingForTest(); },
      [this, label, expected_term]() {
        Check(label + QStringLiteral("-error"),
              dialog_->ErrorTextForTest().contains(expected_term),
              dialog_->ErrorTextForTest());
        Check(label + QStringLiteral("-requests"),
              dialog_->LastHttpRequestsForTest() == 1,
              QStringLiteral("last=%1")
                  .arg(dialog_->LastHttpRequestsForTest()));
        Check(label + QStringLiteral("-confirm-off"),
              !dialog_->ConfirmEnabledForTest());
        Check(label + QStringLiteral("-no-playback"), !window_->HasMediaForTest());
        Log(label + QStringLiteral(" error=%1").arg(dialog_->ErrorTextForTest()));
      },
      [this]() { dialog_->CancelForTest(); });
}

void OnlineResolveTestRunner::StepMismatch() {
  if (dialog_ == nullptr) {
    return;
  }
  dialog_->SetHttpClientForTest(nullptr);
  dialog_->SetInputForTest(QStringLiteral("BV1mA411m7pX"));
  dialog_->StartResolveForTest();
  PollUntil(
      QStringLiteral("mismatch"), kLiveTimeoutMs,
      [this]() { return !dialog_->IsResolvingForTest(); },
      [this]() {
        const QString title = dialog_->TitleTextForTest();
        Check(QStringLiteral("mismatch-no-error"),
              dialog_->ErrorTextForTest().isEmpty(), dialog_->ErrorTextForTest());
        Check(QStringLiteral("mismatch-title-nonempty"), !title.isEmpty(), title);
        Check(QStringLiteral("mismatch-title-differs"), title != ob1_title_,
              QStringLiteral("actual=%1 ob1=%2").arg(title, ob1_title_));
        Check(QStringLiteral("mismatch-duration-nonempty"),
              !dialog_->TotalDurationTextForTest().isEmpty(),
              dialog_->TotalDurationTextForTest());
        Check(QStringLiteral("mismatch-duration-differs"),
              dialog_->TotalDurationTextForTest() != QStringLiteral("01:51"),
              dialog_->TotalDurationTextForTest());
        Check(QStringLiteral("mismatch-requests"),
              dialog_->LastHttpRequestsForTest() == 1);
        Check(QStringLiteral("mismatch-confirm-enabled"),
              dialog_->ConfirmEnabledForTest());
        Check(QStringLiteral("mismatch-not-confirmed"),
              !dialog_->ConfirmedForTest());
        Check(QStringLiteral("mismatch-no-playback"),
              !window_->HasMediaForTest());
        SaveShot(QStringLiteral("mismatch"));
        Log(QStringLiteral("mismatch title=%1 duration=%2 current=%3")
                .arg(title)
                .arg(dialog_->TotalDurationTextForTest())
                .arg(dialog_->CurrentPageTextForTest()));

        const int before = confirmed_events_;
        dialog_->ClickConfirmForTest();
        Check(QStringLiteral("mismatch-confirmed"), dialog_->ConfirmedForTest());
        Check(QStringLiteral("mismatch-confirm-event"),
              confirmed_events_ == before + 1 && last_confirmed_bvid_ ==
                                                    QStringLiteral("BV1mA411m7pX") &&
                  last_confirmed_cid_ > 0 && last_confirmed_page_ == 1,
              QStringLiteral("events=%1 bvid=%2 cid=%3 page=%4")
                  .arg(confirmed_events_)
                  .arg(last_confirmed_bvid_)
                  .arg(last_confirmed_cid_)
                  .arg(last_confirmed_page_));
      },
      [this]() { dialog_->CancelForTest(); });
}

void OnlineResolveTestRunner::StepCancelFake() {
  if (dialog_ == nullptr) {
    return;
  }
  auto fake = std::make_unique<SlowFakeHttpClient>(5000);
  auto* fake_ptr = fake.get();
  dialog_->SetHttpClientForTest(std::move(fake));
  dialog_->SetInputForTest(QStringLiteral("BV1BMnv6jE5S"));
  dialog_->StartResolveForTest();
  Check(QStringLiteral("cancel-fake-started"), dialog_->IsResolvingForTest());
  QTimer::singleShot(100, this, [this, fake_ptr]() {
    const qint64 cancel_ms = dialog_->CancelForTest();
    Check(QStringLiteral("cancel-fake-le-100ms"),
          cancel_ms >= 0 && cancel_ms <= 100,
          QStringLiteral("cancel_ms=%1").arg(cancel_ms));
    Log(QStringLiteral("cancel_fake cancel_ms=%1 requests=%2")
            .arg(cancel_ms)
            .arg(fake_ptr->requests()));
    Check(QStringLiteral("cancel-fake-requests"), fake_ptr->requests() == 1,
          QStringLiteral("requests=%1").arg(fake_ptr->requests()));
    Check(QStringLiteral("cancel-fake-not-resolving"),
          !dialog_->IsResolvingForTest());
    Check(QStringLiteral("cancel-fake-no-worker"),
          !dialog_->HasWorkerThreadForTest());
    Check(QStringLiteral("cancel-fake-status"),
          dialog_->StatusTextForTest().contains(QStringLiteral("取消")),
          dialog_->StatusTextForTest());
    Check(QStringLiteral("cancel-fake-confirm-off"),
          !dialog_->ConfirmEnabledForTest());
    dialog_->SetHttpClientForTest(nullptr);
    RunNext();
  });
}

void OnlineResolveTestRunner::StepCloseFake() {
  if (dialog_ == nullptr) {
    return;
  }
  auto fake = std::make_unique<SlowFakeHttpClient>(5000);
  auto* fake_ptr = fake.get();
  dialog_->SetHttpClientForTest(std::move(fake));
  dialog_->SetInputForTest(QStringLiteral("BV1BMnv6jE5S"));
  dialog_->StartResolveForTest();
  Check(QStringLiteral("close-fake-started"), dialog_->IsResolvingForTest());
  QTimer::singleShot(100, this, [this, fake_ptr]() {
    QElapsedTimer timer;
    timer.start();
    dialog_->CloseForTest();
    const qint64 close_ms = timer.elapsed();
    Check(QStringLiteral("close-fake-le-100ms"), close_ms <= 100,
          QStringLiteral("close_ms=%1").arg(close_ms));
    Log(QStringLiteral("close_fake close_ms=%1 cancel_ms=%2 requests=%3")
            .arg(close_ms)
            .arg(dialog_->LastCancelMsForTest())
            .arg(fake_ptr->requests()));
    Check(QStringLiteral("close-fake-hidden"), !dialog_->isVisible());
    Check(QStringLiteral("close-fake-not-resolving"),
          !dialog_->IsResolvingForTest());
    Check(QStringLiteral("close-fake-no-worker"),
          !dialog_->HasWorkerThreadForTest());
    Check(QStringLiteral("close-fake-cancel-ms"),
          dialog_->LastCancelMsForTest() >= 0 &&
              dialog_->LastCancelMsForTest() <= 100,
          QStringLiteral("cancel_ms=%1").arg(dialog_->LastCancelMsForTest()));
    Check(QStringLiteral("close-fake-requests"), fake_ptr->requests() == 1,
          QStringLiteral("requests=%1").arg(fake_ptr->requests()));
    window_->OpenOnlineDialogForTest();
    Check(QStringLiteral("close-fake-reopen"), dialog_->isVisible());
    dialog_->SetHttpClientForTest(nullptr);
    RunNext();
  });
}

}  // namespace lapian::app
