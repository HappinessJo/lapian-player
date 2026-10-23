#pragma once

// T8.4 可增长读层探针（--online-growing-test，无 GUI、离线）。
//
// 只走 GrowingSource/GrowingCursor/GrowingFormat + CacheStore 路径：
// 临时缓存根、本地 faststart 样本（mp4 moov 在前），**不访问网络、不需要 SESSDATA**。
// 覆盖 next-session-prompt §5 的 DoD：0.3MB 首帧、100 次 seek 落点 0、
// 已缓存 seek 零阻塞、未缓存等待 ≈ 剩余/速率、取消 ≤100ms、4 读者压测、
// 关窗 ≤1s、内存有界、无 busy loop、EOF/EIO/EXIT、淘汰 busy。
//
// 输出（key=value）：sample/第一帧/seek/等待/取消/压测/关闭/内存/CPU/淘汰/verdict。
// 退出码：0 = verdict=pass；1 = 有失败；2 = 样本/临时目录不可用。

#include <QString>

namespace lapian::app {

struct OnlineGrowingTestOptions {
  QString media;   // faststart 样本；空 = build/test-media/faststart-60s.mp4
  QString root;    // 空 = 临时目录；只用于调试
  bool keep_root = false;
  int minutes = 0;  // 0 = 短版（10s 压测）；10 = 真实 10 分钟
  int readers = 4;  // 1..8
};

int RunOnlineGrowingTest(const OnlineGrowingTestOptions& options);

}  // namespace lapian::app
