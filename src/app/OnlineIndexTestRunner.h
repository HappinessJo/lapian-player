#pragma once

#include <QString>

#include <functional>

class MainWindow;

namespace lapian::app {

struct OnlineIndexTestOptions {
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  int page = 1;
  QString tag;
  int timeout_ms = 60000;
  int seeks = 20;
  // >0：测试用下载节流（字节/秒），复现 1–2MB/s 的“边下边索引/波形”。
  qint64 throttle_bytes_per_sec = 0;
  // 非空：先跑受控慢增长离线 case（本地缓存文件按 throttle 速率 Publish），
  // 不联网；跑完直接退出。
  QString offline_video;
  QString offline_audio;
  QString sessdata_file;
};

// T8.11 渐进索引/波形/总帧数验收：
// - 真实在线：首帧 → 40s 内索引可用、30s 缓存时稳定帧覆盖、缓存区 seek 落点 0、
//   完成后精确总帧数、未缓存 seek 提示、波形渐进/偏移、UI 停顿 ≤200ms；
// - `--index-offline-video/--index-offline-audio`：本地缓存按 1–2MB/s 受控增长，
//   验证增量索引不从头全扫、stable 单调、complete 精确、波形只追加不重置。
class OnlineIndexTestRunner {
 public:
  OnlineIndexTestRunner(MainWindow* window, OnlineIndexTestOptions options);
  void Start();

 private:
  void Run();
  void RunOfflineSlow();
  bool PumpUntil(const std::function<bool()>& predicate, int timeout_ms,
                 const char* label);
  void PumpFor(int ms);
  void Check(const QString& name, bool ok, const QString& detail = QString());
  void Finish();
  bool StartAndWaitReady(const QString& bvid, const QString& title,
                         qint64 duration_ms, const QString& sessdata,
                         int timeout_ms, double* ready_ms);

  MainWindow* window_ = nullptr;
  OnlineIndexTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace lapian::app
