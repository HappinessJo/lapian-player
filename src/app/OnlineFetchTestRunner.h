#pragma once

// T8.5 取流探针（无 GUI）：
// - 默认离线：fake transport + 本地 faststart 样本；不联网、不需要凭据；
// - `--real`：真实 playurl + CDN；只从 .secrets/LAPIAN_SESSDATA_FILE/--sessdata-file
//   读取 SESSDATA；绝不打印凭据或签名 URL。
//
// 输出稳定 key=value；退出码 0=pass，1=fail，2=blocked/环境不满足。

#include <QString>

#include <cstdint>

namespace lapian::app {

struct OnlineFetchTestOptions {
  bool real = false;
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  QString tag;
  QString sessdata_file;
  QString media;
  QString root;
  bool keep_root = false;
  // 真实断点续传模拟：下载到约 N 字节时直接 _Exit(0)（不清理 .part）。
  qint64 exit_after_bytes = 0;
  bool cancel_after_first_frame = false;
  bool qn_1080 = false;
  // 测试用参数矩阵（T8.8）：qn>0 覆盖旧 --qn-1080 映射；fourk 透传 playurl。
  // 只有探针使用；产品路径不受影响。
  int qn = 0;
  int fourk = 0;
  // 真实故障注入（测试用）：把主 URL / 全部 URL 的 deadline 改坏，触发 403。
  // 不打印 URL；刷新回调会重新取流得到合法地址。
  QString break_urls;  // "" / "primary" / "all"
  int timeout_ms = 20000;
};

int RunOnlineFetchTest(const OnlineFetchTestOptions& options);

}  // namespace lapian::app
