#pragma once

// T8.2.2 扫码登录探针（无 GUI）：
// - 默认离线：fake HTTP 队列，覆盖 4 态/过期/取消/坏 JSON/412/429/只保留 SESSDATA；
// - `--real-generate`：只做真实 generate + 一次 poll（不扫码、不保存凭据），
//   用于验证官方字段；真实扫码仍需用户手机。
//
// 输出稳定 key=value；退出码 0=pass，1=fail，2=blocked/环境不满足。

#include <QString>

namespace lapian::app {

struct OnlineQrTestOptions {
  bool real_generate = false;
  // 额外的 offscreen UI smoke（需要 QApplication；由 main 在 --ui 时创建）。
  bool ui_smoke = false;
  int timeout_ms = 6000;
};

int RunOnlineQrTest(const OnlineQrTestOptions& options);

}  // namespace lapian::app
