#pragma once

#include <QObject>
#include <QString>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

struct AVFrame;

// T5.4 截图后台服务：UI 线程只对“按键瞬间已显示的帧”做 av_frame_ref，
// 工作线程负责硬解帧回读、YUV→BGRA、按 display matrix 旋转、WIC PNG 编码与写盘。
// 所有任务串行执行；重名由 QFile::NewOnly 保证不覆盖。
class SnapshotService : public QObject {
  Q_OBJECT

 public:
  struct Request {
    QString video_path;
    std::int64_t frame = 0;
    int rotation_counterclockwise_degrees = 0;
    int fps_num = 0;
    int fps_den = 0;
  };

  explicit SnapshotService(QString pictures_root = QString(), QObject* parent = nullptr);
  ~SnapshotService() override;

  // 从调用线程持有一份 AVFrame 引用（O(1)），成功入队返回 true；队列满返回 false。
  bool Capture(const AVFrame* frame, const Request& request);

  // 自动化/测试注入图片回退根目录；须在截图任务之前调用。
  void SetPicturesRoot(const QString& root);

  // 等待队列清空且当前任务完成；切文件/关窗前调用，避免工作线程持有旧解码器资源。
  void Drain();

  int PendingCount() const;

 signals:
  void snapshotSaved(const QString& path, bool used_fallback, qint64 frame_index,
                     qint64 encode_ms);
  void snapshotFailed(const QString& error);

 private:
  struct Task {
    AVFrame* frame = nullptr;
    Request request;
  };

  void WorkerLoop();
  bool ProcessTask(const Task& task, QString* path, bool* used_fallback, QString* error,
                   qint64* encode_ms);

  QString pictures_root_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable idle_cv_;
  std::deque<Task> queue_;
  bool stopping_ = false;
  bool busy_ = false;
  std::thread thread_;
};
