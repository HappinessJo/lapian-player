#include "app/RobustTestRunner.h"

#include <windows.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

#include "app/MainWindow.h"

namespace lapian::app {
namespace {

constexpr int kOpenBudgetMs = 5000;
constexpr int kTotalBudgetMs = 90000;

QString LogText(QString text) {
  text.replace(QLatin1Char('\n'), QLatin1Char(' '));
  if (text.size() > 220) {
    text.truncate(220);
  }
  return text;
}

}  // namespace

RobustTestRunner::RobustTestRunner(MainWindow* window, Options options, QObject* parent)
    : QObject(parent == nullptr ? window : parent),
      window_(window),
      options_(std::move(options)) {
  if (options_.tag.isEmpty()) {
    options_.tag = QStringLiteral("default");
  }
  if (options_.only.isEmpty()) {
    options_.only = QStringLiteral("all");
  }
}

void RobustTestRunner::Start() {
  total_timer_.start();
  std::fprintf(stderr, "T7.5 robust_begin tag=%s media=%s root=%s only=%s\n",
               options_.tag.toUtf8().constData(), options_.media.toUtf8().constData(),
               options_.root.toUtf8().constData(), options_.only.toUtf8().constData());
  std::fflush(stderr);
  if (!PrepareFixtures()) {
    Fail(QStringLiteral("测试夹具准备失败"));
    return;
  }
  InstallModalGuard();

  // 总超时保护：任何一步卡死都不能让探针永远挂着（单步预算另有 5s 断言）。
  QTimer::singleShot(kTotalBudgetMs, this, [this]() {
    Fail(QStringLiteral("总超时 %1s").arg(kTotalBudgetMs / 1000));
  });

  Enqueue(QStringLiteral("baseline"), [this]() { RunBaseline(); });
  if (options_.only == QStringLiteral("all")) {
    Enqueue(QStringLiteral("device-loss"), [this]() { RunDeviceLoss(); });
    Enqueue(QStringLiteral("bad-files"), [this]() { RunBadCases(); });
    Enqueue(QStringLiteral("locked-file"), [this]() { RunLockedFile(); });
    Enqueue(QStringLiteral("truncated-file"), [this]() { RunTruncatedFile(); });
    Enqueue(QStringLiteral("long-path"), [this]() { RunLongPath(); });
    Enqueue(QStringLiteral("readonly"), [this]() { RunReadOnly(); });
    Enqueue(QStringLiteral("network"), [this]() { RunNetwork(); });
  } else if (options_.only == QStringLiteral("readonly")) {
    Enqueue(QStringLiteral("readonly"), [this]() { RunReadOnly(); });
  } else if (options_.only == QStringLiteral("network")) {
    Enqueue(QStringLiteral("network"), [this]() { RunNetwork(); });
  } else {
    Fail(QStringLiteral("未知 --robust-only：") + options_.only);
    return;
  }
  RunNext();
}

void RobustTestRunner::Enqueue(const QString& name, Step step) {
  steps_.emplace_back(name, std::move(step));
}

void RobustTestRunner::RunNext() {
  if (finished_ || failed_) {
    return;
  }
  if (steps_.empty()) {
    Finish();
    return;
  }
  auto entry = std::move(steps_.front());
  steps_.pop_front();
  std::fprintf(stderr, "T7.5 robust_step name=%s\n", entry.first.toUtf8().constData());
  std::fflush(stderr);
  entry.second();
}

void RobustTestRunner::Check(const QString& name, bool ok, const QString& detail) {
  ++checks_;
  if (!ok) {
    ++failures_;
  }
  const QByteArray detail_text = detail.isEmpty()
                                     ? QByteArray()
                                     : QByteArray(" detail=") + LogText(detail).toUtf8();
  std::fprintf(stderr, "T7.5 robust_check %s ok=%d%s\n", name.toUtf8().constData(),
               ok ? 1 : 0, detail_text.constData());
  std::fflush(stderr);
}

void RobustTestRunner::Skip(const QString& name, const QString& reason) {
  ++skips_;
  std::fprintf(stderr, "T7.5 robust_skip %s reason=%s\n", name.toUtf8().constData(),
               reason.toUtf8().constData());
  std::fflush(stderr);
}

void RobustTestRunner::Fail(const QString& reason) {
  if (finished_ || failed_) {
    return;
  }
  failed_ = true;
  infra_failed_ = true;
  std::fprintf(stderr, "T7.5 robust_fail tag=%s reason=%s\n",
               options_.tag.toUtf8().constData(), reason.toUtf8().constData());
  std::fflush(stderr);
  Finish();
}

void RobustTestRunner::Finish() {
  if (finished_) {
    return;
  }
  finished_ = true;
  if (locked_handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(locked_handle_));
    locked_handle_ = nullptr;
  }
  window_->SimulateAudioDeviceLossForTest(false);
  const double seconds = static_cast<double>(total_timer_.elapsed()) / 1000.0;
  const bool pass = failures_ == 0 && !infra_failed_;
  std::fprintf(stderr,
               "T7.5 robust_summary tag=%s checks=%d skips=%d failures=%d "
               "modal_dismissed=%d elapsed_s=%.2f pass=%d\n",
               options_.tag.toUtf8().constData(), checks_, skips_, failures_,
               modal_dismissed_, seconds, pass ? 1 : 0);
  std::fflush(stderr);
  QCoreApplication::exit(pass ? 0 : (infra_failed_ ? 2 : 1));
}

void RobustTestRunner::InstallModalGuard() {
  auto* timer = new QTimer(this);
  timer->setInterval(25);
  connect(timer, &QTimer::timeout, this, &RobustTestRunner::PollModalGuard);
  timer->start();
}

void RobustTestRunner::PollModalGuard() {
  QWidget* modal = QApplication::activeModalWidget();
  if (modal == nullptr) {
    return;
  }
  if (auto* box = qobject_cast<QMessageBox*>(modal)) {
    ++modal_dismissed_;
    std::fprintf(stderr, "T7.5 robust_modal title=%s text=%s\n",
                 box->windowTitle().toUtf8().constData(),
                 LogText(box->text()).toUtf8().constData());
    std::fflush(stderr);
    box->done(QMessageBox::Cancel);
    return;
  }
  // 原生文件对话框等不应出现在 --robust-test；记录一次便于排查。
  ++modal_dismissed_;
  std::fprintf(stderr, "T7.5 robust_modal_unknown class=%s\n",
               modal->metaObject()->className());
  std::fflush(stderr);
  modal->close();
}

bool RobustTestRunner::PrepareFixtures() {
  if (!QFileInfo::exists(options_.media)) {
    std::fprintf(stderr, "T7.5 robust_fail media_missing=%s\n",
                 options_.media.toUtf8().constData());
    return false;
  }
  baseline_name_ = QFileInfo(options_.media).fileName();
  QDir root(options_.root);
  root.removeRecursively();
  if (!QDir().mkpath(options_.root)) {
    return false;
  }
  if (!options_.pictures_root.isEmpty()) {
    QDir().mkpath(options_.pictures_root);
  }

  if (!WriteBytes(QStringLiteral("empty.mp4"), QByteArray())) {
    return false;
  }
  QByteArray garbage(4096, '\0');
  for (int index = 0; index < garbage.size(); ++index) {
    garbage[index] = static_cast<char>((index * 37 + 11) & 0xff);
  }
  if (!WriteBytes(QStringLiteral("garbage.mp4"), garbage)) {
    return false;
  }
  if (!WriteBytes(QStringLiteral("text.mp4"), QByteArray("this is not a video\n"))) {
    return false;
  }

  bad_cases_.push_back({QStringLiteral("missing"), RootPath(QStringLiteral("missing.mp4")),
                        {QStringLiteral("文件不存在")}});
  bad_cases_.push_back({QStringLiteral("empty"), RootPath(QStringLiteral("empty.mp4")),
                        {QStringLiteral("损坏"), QStringLiteral("无法解析"),
                         QStringLiteral("打开失败")}});
  bad_cases_.push_back({QStringLiteral("garbage"), RootPath(QStringLiteral("garbage.mp4")),
                        {QStringLiteral("损坏"), QStringLiteral("无法解析"),
                         QStringLiteral("打开失败")}});
  bad_cases_.push_back({QStringLiteral("text"), RootPath(QStringLiteral("text.mp4")),
                        {QStringLiteral("损坏"), QStringLiteral("不支持"),
                         QStringLiteral("打开失败")}});

  // 截断：proxy-V1 的 moov 在文件尾，截断后 probe 会快速失败；没有该素材时跳过。
  const QString trunc_source =
      QDir::currentPath() + QStringLiteral("/media/proxy/proxy-V1.mp4");
  QString truncated;
  if (QFileInfo::exists(trunc_source) &&
      CopyFixture(trunc_source, QStringLiteral("truncated.mp4"), &truncated)) {
    QFile file(truncated);
    if (file.open(QIODevice::ReadWrite)) {
      file.resize(std::max<qint64>(1024, file.size() / 10));
      file.close();
      bad_cases_.push_back({QStringLiteral("truncated"), truncated,
                            {QStringLiteral("损坏"), QStringLiteral("无法解析"),
                             QStringLiteral("打开失败")}});
    }
  }

  const QString wav = QStringLiteral("C:\\Windows\\Media\\Alarm01.wav");
  const QString wav_fallback = QStringLiteral("C:\\Windows\\Media\\chimes.wav");
  if (QFileInfo::exists(wav) || QFileInfo::exists(wav_fallback)) {
    bad_cases_.push_back({QStringLiteral("no-video"),
                          QFileInfo::exists(wav) ? wav : wav_fallback,
                          {QStringLiteral("损坏"), QStringLiteral("不支持"),
                           QStringLiteral("没有视频流"), QStringLiteral("打开失败")}});
  } else {
    Skip(QStringLiteral("bad-no-video"), QStringLiteral("系统 WAV 素材缺失"));
  }

  bad_cases_.push_back({QStringLiteral("directory"), options_.root,
                        {QStringLiteral("文件夹"), QStringLiteral("没有读取权限"),
                         QStringLiteral("无法打开"), QStringLiteral("打开失败")}});

  if (!CopyFixture(options_.media, QStringLiteral("locked.mp4"), &locked_video_)) {
    return false;
  }
  if (!CopyFixture(options_.media, QStringLiteral("truncate-open.mp4"), &truncate_video_)) {
    return false;
  }
  readonly_video_ = QDir(options_.read_only_dir).filePath(QStringLiteral("readonly.mp4"));
  return true;
}

QString RobustTestRunner::RootPath(const QString& name) const {
  return QDir(options_.root).filePath(name);
}

bool RobustTestRunner::WriteBytes(const QString& name, const QByteArray& bytes) {
  QFile file(RootPath(name));
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  return file.write(bytes) == bytes.size();
}

bool RobustTestRunner::CopyFixture(const QString& source, const QString& name,
                                   QString* out_path) {
  const QString target = RootPath(name);
  if (!QFile::copy(source, target)) {
    // 重复运行/上次残留时先删再复制。
    QFile::remove(target);
    if (!QFile::copy(source, target)) {
      return false;
    }
  }
  if (out_path != nullptr) {
    *out_path = target;
  }
  return true;
}

bool RobustTestRunner::CreateLongPathTree(QString* out_video) {
  QString dir = options_.root;
  const QString segment = QStringLiteral("很长的中文目录名称_用于测试长路径_ABCDEFGHIJ");
  for (int index = 0; index < 8; ++index) {
    dir += QLatin1Char('/') + segment + QString::number(index);
  }
  if (!QDir().mkpath(dir)) {
    return false;
  }
  QString target = QDir(dir).filePath(QStringLiteral("测试视频_长路径.mp4"));
  if (target.size() < 260) {
    // 8 段中文已超过 260；不足时补一段，保证确实覆盖长路径。
    dir += QLatin1Char('/') + segment + QStringLiteral("extra");
    if (!QDir().mkpath(dir)) {
      return false;
    }
    target = QDir(dir).filePath(QStringLiteral("测试视频_长路径.mp4"));
  }
  if (!QFile::copy(options_.media, target)) {
    QFile::remove(target);
    if (!QFile::copy(options_.media, target)) {
      return false;
    }
  }
  if (out_video != nullptr) {
    *out_video = target;
  }
  std::fprintf(stderr, "T7.5 robust_long_path length=%lld path=%s\n",
               static_cast<long long>(target.size()), target.toUtf8().constData());
  std::fflush(stderr);
  return true;
}

void RobustTestRunner::PollUntil(const QString& what, int timeout_ms,
                                 std::function<bool()> predicate, Step on_ready,
                                 Step on_timeout) {
  auto started = std::make_shared<QElapsedTimer>();
  started->start();
  auto tick = std::make_shared<std::function<void()>>();
  *tick = [this, what, timeout_ms, predicate = std::move(predicate),
           on_ready = std::move(on_ready), on_timeout = std::move(on_timeout),
           started, tick]() {
    if (finished_ || failed_) {
      return;
    }
    if (predicate()) {
      on_ready();
      return;
    }
    if (started->elapsed() >= timeout_ms) {
      if (on_timeout) {
        on_timeout();
        return;
      }
      const QString error = window_->LastOpenErrorForTest();
      Fail(what + QStringLiteral(" 超时（%1ms）").arg(started->elapsed()) +
           (error.isEmpty() ? QString() : QStringLiteral("；") + LogText(error)));
      return;
    }
    QTimer::singleShot(30, this, [tick]() { (*tick)(); });
  };
  QTimer::singleShot(0, this, [tick]() { (*tick)(); });
}

void RobustTestRunner::OpenAndWait(const QString& path, Step on_ready) {
  window_->OpenFile(path);
  const QString expected = QFileInfo(path).fileName();
  PollUntil(QStringLiteral("打开 ") + expected, 10000,
            [this, expected]() {
              return window_->HasMediaForTest() &&
                     window_->InfoFileNameForTest() == expected &&
                     window_->IndexReadyForTest();
            },
            std::move(on_ready));
}

void RobustTestRunner::LogState(const char* step) const {
  std::fprintf(stderr,
               "T7.5 robust_state step=%s media=%d name=%s frame=%lld last=%lld "
               "index=%d playing=%d audio=%s marks=%d banner=%s\n",
               step, window_->HasMediaForTest() ? 1 : 0,
               window_->InfoFileNameForTest().toUtf8().constData(),
               static_cast<long long>(window_->CurrentFrameForTest()),
               static_cast<long long>(window_->LastFrameIndexForTest()),
               window_->IndexReadyForTest() ? 1 : 0, window_->IsPlayingForTest() ? 1 : 0,
               window_->AudioStateForTest().toUtf8().constData(),
               window_->MarkCountForTest(),
               LogText(window_->BannerStateForTest()).toUtf8().constData());
  std::fflush(stderr);
}

void RobustTestRunner::RunBaseline() {
  QElapsedTimer timer;
  timer.start();
  window_->OpenFile(options_.media);
  const double open_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  Check(QStringLiteral("baseline-open-5s"), open_ms <= kOpenBudgetMs,
        QStringLiteral("open_ms=%1").arg(open_ms, 0, 'f', 1));
  PollUntil(
      QStringLiteral("baseline 打开"), 12000,
      [this]() {
        return window_->HasMediaForTest() &&
               window_->InfoFileNameForTest() == baseline_name_ &&
               window_->IndexReadyForTest();
      },
      [this]() {
        CheckBaseline();
        RunNext();
      });
}

void RobustTestRunner::CheckBaseline() {
  baseline_frame_ = window_->CurrentFrameForTest();
  baseline_last_frame_ = window_->LastFrameIndexForTest();
  Check(QStringLiteral("baseline-media"), window_->HasMediaForTest());
  Check(QStringLiteral("baseline-name"), window_->InfoFileNameForTest() == baseline_name_,
        window_->InfoFileNameForTest());
  Check(QStringLiteral("baseline-frame0"), baseline_frame_ == 0,
        QStringLiteral("frame=%1").arg(baseline_frame_));
  Check(QStringLiteral("baseline-not-playing"), !window_->IsPlayingForTest());
  Check(QStringLiteral("baseline-frames"), baseline_last_frame_ >= 10,
        QStringLiteral("last=%1").arg(baseline_last_frame_));
  Check(QStringLiteral("baseline-display"),
        window_->SnapshotDisplaySizeForTest().width() > 0 &&
            window_->SnapshotDisplaySizeForTest().height() > 0);
  LogState("baseline");
}

void RobustTestRunner::RunDeviceLoss() {
  const QString initial = window_->AudioStateForTest();
  if (initial == QStringLiteral("none")) {
    Skip(QStringLiteral("device-loss"), QStringLiteral("素材无音轨"));
    RunNext();
    return;
  }
  if (initial == QStringLiteral("error")) {
    Skip(QStringLiteral("device-loss"),
         QStringLiteral("音频设备不可用（headless/无输出）"));
    RunNext();
    return;
  }

  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  window_->StartPlaybackForTest();

  // 音频设备打开慢/不可用时跳过，不把环境问题算成产品失败。
  auto audio_ready = std::make_shared<QElapsedTimer>();
  audio_ready->start();
  auto wait_audio = std::make_shared<std::function<void()>>();
  *wait_audio = [this, audio_ready, wait_audio]() {
    const QString state = window_->AudioStateForTest();
    const bool playing = window_->IsPlayingForTest();
    if (playing && state == QStringLiteral("playing")) {
      Check(QStringLiteral("device-loss-start-playing"), true);
      QTimer::singleShot(400, this, [this]() {
        const qint64 frame_at_loss = window_->CurrentFrameForTest();
        window_->SimulateAudioDeviceLossForTest(true);
        PollUntil(
            QStringLiteral("设备丢失静音"), 2500,
            [this]() { return window_->AudioStateForTest() == QStringLiteral("silent"); },
            [this, frame_at_loss]() {
              Check(QStringLiteral("device-loss-silent"), true);
              QTimer::singleShot(700, this, [this, frame_at_loss]() {
                const qint64 advanced = window_->CurrentFrameForTest();
                Check(QStringLiteral("device-loss-video-continues"), advanced > frame_at_loss,
                      QStringLiteral("from=%1 to=%2").arg(frame_at_loss).arg(advanced));
                Check(QStringLiteral("device-loss-still-playing"),
                      window_->IsPlayingForTest());
                window_->SimulateAudioDeviceLossForTest(false);
                PollUntil(
                    QStringLiteral("设备恢复"), 6000,
                    [this]() {
                      return window_->AudioStateForTest() == QStringLiteral("playing");
                    },
                    [this]() {
                      Check(QStringLiteral("device-loss-recovered"), true);
                      QTimer::singleShot(300, this, [this]() {
                        Check(QStringLiteral("device-loss-recovered-playing"),
                              window_->IsPlayingForTest());
                        window_->TogglePlaybackForTest();
                        window_->SimulateAudioDeviceLossForTest(false);
                        // 恢复后回到第 0 帧，后续坏文件用例以“旧会话/旧帧保留”为准。
                        window_->GoToFrameForTest(0);
                        baseline_frame_ = window_->CurrentFrameForTest();
                        RunNext();
                      });
                    });
              });
            });
      });
      return;
    }
    if (audio_ready->elapsed() > 4000) {
      window_->SimulateAudioDeviceLossForTest(false);
      if (window_->IsPlayingForTest()) {
        window_->TogglePlaybackForTest();
      }
      Skip(QStringLiteral("device-loss"),
           QStringLiteral("无音频输出，state=%1").arg(state));
      RunNext();
      return;
    }
    QTimer::singleShot(50, this, [wait_audio]() { (*wait_audio)(); });
  };
  (*wait_audio)();
}

void RobustTestRunner::RunBadCases() {
  Check(QStringLiteral("bad-cases-present"), !bad_cases_.empty());
  RunBadCase(0);
}

void RobustTestRunner::RunBadCase(std::size_t index) {
  if (index >= bad_cases_.size()) {
    FinishBadCases();
    return;
  }
  const BadCase& item = bad_cases_[index];
  const QByteArray label_step = QByteArray("before-") + item.label.toUtf8();
  LogState(label_step.constData());
  QElapsedTimer timer;
  timer.start();
  window_->OpenFile(item.path);
  const double elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  const QString error = window_->LastOpenErrorForTest();
  const QString banner = window_->BannerStateForTest();
  const bool kept_media =
      window_->HasMediaForTest() && window_->InfoFileNameForTest() == baseline_name_;
  const bool kept_frame = window_->CurrentFrameForTest() == baseline_frame_;

  bool term_ok = !error.isEmpty();
  if (!item.expected_terms.isEmpty()) {
    term_ok = false;
    for (const QString& term : item.expected_terms) {
      if (error.contains(term)) {
        term_ok = true;
        break;
      }
    }
  }
  const bool banner_ok = banner.contains(QStringLiteral("kind=error")) &&
                         banner.contains(QStringLiteral("visible=1"));
  const bool pass = elapsed_ms <= kOpenBudgetMs && kept_media && kept_frame &&
                    term_ok && banner_ok;
  std::fprintf(stderr,
               "T7.5 robust_case label=%s elapsed_ms=%.1f pass=%d kept_media=%d "
               "kept_frame=%d term=%d banner=%d error=%s\n",
               item.label.toUtf8().constData(), elapsed_ms, pass ? 1 : 0,
               kept_media ? 1 : 0, kept_frame ? 1 : 0, term_ok ? 1 : 0,
               banner_ok ? 1 : 0, LogText(error).toUtf8().constData());
  std::fflush(stderr);
  Check(QStringLiteral("bad-%1-5s").arg(item.label), elapsed_ms <= kOpenBudgetMs,
        QStringLiteral("%1ms").arg(elapsed_ms, 0, 'f', 1));
  Check(QStringLiteral("bad-%1-keeps-session").arg(item.label), kept_media);
  Check(QStringLiteral("bad-%1-keeps-frame").arg(item.label), kept_frame);
  Check(QStringLiteral("bad-%1-readable").arg(item.label), term_ok, LogText(error));
  Check(QStringLiteral("bad-%1-banner").arg(item.label), banner_ok, banner);

  QTimer::singleShot(0, this, [this, index]() { RunBadCase(index + 1); });
}

void RobustTestRunner::FinishBadCases() {
  // 错误之后回到基准素材：错误横幅必须清除，导航仍然可用（没有冻结/半死状态）。
  OpenAndWait(options_.media, [this]() {
    Check(QStringLiteral("bad-files-banner-cleared"),
          !window_->BannerStateForTest().contains(QStringLiteral("kind=error")),
          window_->BannerStateForTest());
    const qint64 target = std::min<qint64>(5, baseline_last_frame_);
    window_->GoToFrameForTest(target);
    Check(QStringLiteral("bad-files-ui-alive"), window_->CurrentFrameForTest() == target,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    window_->GoToFrameForTest(0);
    LogState("after-bad-cases");
    RunNext();
  });
}

void RobustTestRunner::RunLockedFile() {
  const std::wstring wide = locked_video_.toStdWString();
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    Skip(QStringLiteral("locked-file"), QStringLiteral("无法建立独占句柄"));
    RunNext();
    return;
  }
  locked_handle_ = handle;
  QElapsedTimer timer;
  timer.start();
  window_->OpenFile(locked_video_);
  const double elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  const QString error = window_->LastOpenErrorForTest();
  Check(QStringLiteral("locked-open-5s"), elapsed_ms <= kOpenBudgetMs,
        QStringLiteral("%1ms").arg(elapsed_ms, 0, 'f', 1));
  Check(QStringLiteral("locked-open-readable"), !error.isEmpty(), LogText(error));
  Check(QStringLiteral("locked-open-explained"),
        error.contains(QStringLiteral("占用")) ||
            error.contains(QStringLiteral("权限")),
        LogText(error));
  Check(QStringLiteral("locked-keeps-session"),
        window_->InfoFileNameForTest() == baseline_name_, window_->InfoFileNameForTest());
  CloseHandle(handle);
  locked_handle_ = nullptr;

  OpenAndWait(locked_video_, [this]() {
    Check(QStringLiteral("locked-open-after-release"),
          window_->InfoFileNameForTest() == QFileInfo(locked_video_).fileName());
    OpenAndWait(options_.media, [this]() { RunNext(); });
  });
}

void RobustTestRunner::RunTruncatedFile() {
  OpenAndWait(truncate_video_, [this]() {
    const qint64 last = window_->LastFrameIndexForTest();
    const qint64 target = std::max<qint64>(1, last - 3);
    {
      QFile file(truncate_video_);
      if (!file.open(QIODevice::ReadWrite)) {
        Skip(QStringLiteral("truncated-open"), QStringLiteral("无法重新打开文件做截断"));
        OpenAndWait(options_.media, [this]() { RunNext(); });
        return;
      }
      file.resize(1024);
      file.close();
    }
    QElapsedTimer timer;
    timer.start();
    window_->GoToFrameForTest(target);
    const double elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    const QString nav_error = window_->LastNavigationErrorForTest();
    std::fprintf(stderr,
                 "T7.5 robust_truncated last=%lld target=%lld elapsed_ms=%.1f error=%s\n",
                 static_cast<long long>(last), static_cast<long long>(target),
                 elapsed_ms, LogText(nav_error).toUtf8().constData());
    std::fflush(stderr);
    Check(QStringLiteral("truncated-seek-5s"), elapsed_ms <= kOpenBudgetMs,
          QStringLiteral("%1ms").arg(elapsed_ms, 0, 'f', 1));
    Check(QStringLiteral("truncated-seek-error"), !nav_error.isEmpty(),
          LogText(nav_error));
    Check(QStringLiteral("truncated-no-crash"), window_->HasMediaForTest());
    OpenAndWait(options_.media, [this]() {
      Check(QStringLiteral("truncated-recovered"),
            window_->InfoFileNameForTest() == baseline_name_);
      RunNext();
    });
  });
}

void RobustTestRunner::RunLongPath() {
  QString video;
  if (!CreateLongPathTree(&video)) {
    Skip(QStringLiteral("long-path"), QStringLiteral("无法创建 >260 字符中文路径"));
    RunNext();
    return;
  }
  long_video_ = video;
  OpenAndWait(video, [this]() {
    Check(QStringLiteral("long-path-open"),
          window_->InfoFileNameForTest() == QFileInfo(long_video_).fileName());
    Check(QStringLiteral("long-path-frames"), window_->LastFrameIndexForTest() >= 10,
          QStringLiteral("last=%1").arg(window_->LastFrameIndexForTest()));
    window_->GoToFrameForTest(5);
    Check(QStringLiteral("long-path-seek"), window_->CurrentFrameForTest() == 5,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    window_->AddMarkForTest();
    Check(QStringLiteral("long-path-mark"), window_->MarkCountForTest() == 1);
    Check(QStringLiteral("long-path-mark-sidecar"), !window_->MarkUsingFallbackForTest(),
          window_->MarkStoragePathForTest());
    window_->CaptureSnapshotForTest();
    Check(QStringLiteral("long-path-snapshot-done"), window_->DrainSnapshotsForTest());
    const QStringList paths = window_->SnapshotSavedPathsForTest();
    Check(QStringLiteral("long-path-snapshot-saved"),
          !paths.isEmpty() && QFileInfo::exists(paths.back()),
          paths.isEmpty() ? QStringLiteral("no path") : paths.back());
    const QString video_dir =
        QDir::fromNativeSeparators(QFileInfo(long_video_).absolutePath());
    Check(QStringLiteral("long-path-snapshot-in-place"),
          !paths.isEmpty() &&
              QDir::fromNativeSeparators(paths.back()).startsWith(video_dir),
          paths.isEmpty() ? QString() : paths.back());
    OpenAndWait(options_.media, [this]() { RunNext(); });
  });
}

void RobustTestRunner::RunReadOnly() {
  QString video = readonly_video_;
  QString dir = options_.read_only_dir;
  if (options_.only == QStringLiteral("readonly")) {
    video = options_.media;
    dir = QFileInfo(options_.media).absolutePath();
  }
  if (!QFileInfo::exists(video)) {
    Skip(QStringLiteral("readonly"),
         QStringLiteral("未提供只读素材；用 scripts\\robust-verify.ps1 跑真实 ACL"));
    RunNext();
    return;
  }
  // 确认目录确实不可写：能建 snapshots 说明 ACL 没生效，按 skip 处理而不是假绿。
  const QString probe_dir = QDir(dir).filePath(QStringLiteral("snapshots"));
  if (QDir().mkpath(probe_dir)) {
    QDir(probe_dir).removeRecursively();
    Skip(QStringLiteral("readonly"),
         QStringLiteral("目录仍可写，未设置 ACL；用 scripts\\robust-verify.ps1"));
    RunNext();
    return;
  }
  OpenAndWait(video, [this, video, dir]() {
    Check(QStringLiteral("readonly-open"), window_->HasMediaForTest());
    window_->AddMarkForTest();
    Check(QStringLiteral("readonly-mark-added"), window_->MarkCountForTest() == 1);
    Check(QStringLiteral("readonly-mark-fallback"), window_->MarkUsingFallbackForTest(),
          window_->MarkStoragePathForTest());
    const QString storage = window_->MarkStoragePathForTest();
    const QString appdata =
        QDir::fromNativeSeparators(QDir(options_.root).filePath(QStringLiteral("appdata")));
    Check(QStringLiteral("readonly-mark-appdata"),
          !appdata.isEmpty() && QDir::fromNativeSeparators(storage).startsWith(appdata),
          storage);
    Check(QStringLiteral("readonly-no-sidecar"),
          !QFileInfo::exists(video + QStringLiteral(".lapian.json")),
          video + QStringLiteral(".lapian.json"));
    Check(QStringLiteral("readonly-fallback-file-exists"), QFileInfo::exists(storage),
          storage);
    window_->CaptureSnapshotForTest();
    Check(QStringLiteral("readonly-snapshot-done"), window_->DrainSnapshotsForTest());
    Check(QStringLiteral("readonly-snapshot-fallback"),
          window_->SnapshotLastFallbackForTest(), window_->SnapshotLastErrorForTest());
    const QStringList paths = window_->SnapshotSavedPathsForTest();
    Check(QStringLiteral("readonly-snapshot-saved"),
          !paths.isEmpty() && QFileInfo::exists(paths.back()),
          paths.isEmpty() ? QStringLiteral("no path") : paths.back());
    if (!options_.pictures_root.isEmpty() && !paths.isEmpty()) {
      Check(QStringLiteral("readonly-snapshot-in-pictures"),
            QDir::fromNativeSeparators(paths.back())
                .startsWith(QDir::fromNativeSeparators(options_.pictures_root)),
            paths.back());
    }
    std::fprintf(stderr, "T7.5 robust_readonly dir=%s storage=%s saved=%s\n",
                 dir.toUtf8().constData(), storage.toUtf8().constData(),
                 paths.isEmpty() ? "-" : paths.back().toUtf8().constData());
    std::fflush(stderr);
    if (video != options_.media) {
      OpenAndWait(options_.media, [this]() { RunNext(); });
    } else {
      RunNext();
    }
  });
}

void RobustTestRunner::RunNetwork() {
  // 不可达 UNC：修复前 avformat_open_input 会阻塞 ~21s；现在必须 ≤5s 返回。
  // 每次换一个 TEST-NET-3 地址，避免 Windows SMB 负缓存让第一次预检“秒回”，
  // 从而真正覆盖 3s 超时分支。
  const int last_octet = QRandomGenerator::global()->bounded(1, 255);
  const QString dead = QStringLiteral("\\\\203.0.113.%1\\lapian-t7.5\\missing.mp4")
                           .arg(last_octet);
  QElapsedTimer timer;
  timer.start();
  window_->OpenFile(dead);
  const double dead_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  const QString dead_error = window_->LastOpenErrorForTest();
  const bool dead_kept =
      window_->HasMediaForTest() && window_->InfoFileNameForTest() == baseline_name_;
  const bool dead_readable = dead_error.contains(QStringLiteral("网络")) ||
                             dead_error.contains(QStringLiteral("无响应")) ||
                             dead_error.contains(QStringLiteral("不存在"));
  std::fprintf(stderr,
               "T7.5 robust_network target=dead elapsed_ms=%.1f kept=%d error=%s\n",
               dead_ms, dead_kept ? 1 : 0, LogText(dead_error).toUtf8().constData());
  std::fflush(stderr);
  Check(QStringLiteral("network-dead-5s"), dead_ms <= kOpenBudgetMs,
        QStringLiteral("%1ms").arg(dead_ms, 0, 'f', 1));
  Check(QStringLiteral("network-dead-readable"), dead_readable, LogText(dead_error));
  Check(QStringLiteral("network-dead-keeps-session"), dead_kept);
  Check(QStringLiteral("network-dead-banner"),
        window_->BannerStateForTest().contains(QStringLiteral("kind=error")));

  // 可达主机但共享不存在：快速 permission_denied/not_found，不应等待 SMB 超时。
  const QString loopback =
      QStringLiteral("\\\\127.0.0.1\\lapian-t7.5-missing\\missing.mp4");
  timer.restart();
  window_->OpenFile(loopback);
  const double loopback_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  const QString loopback_error = window_->LastOpenErrorForTest();
  std::fprintf(stderr,
               "T7.5 robust_network target=loopback elapsed_ms=%.1f error=%s\n",
               loopback_ms, LogText(loopback_error).toUtf8().constData());
  std::fflush(stderr);
  Check(QStringLiteral("network-loopback-5s"), loopback_ms <= kOpenBudgetMs,
        QStringLiteral("%1ms").arg(loopback_ms, 0, 'f', 1));
  Check(QStringLiteral("network-loopback-readable"), !loopback_error.isEmpty(),
        LogText(loopback_error));
  Check(QStringLiteral("network-loopback-keeps-session"),
        window_->HasMediaForTest() && window_->InfoFileNameForTest() == baseline_name_);
  LogState("after-network");
  RunNext();
}

}  // namespace lapian::app
