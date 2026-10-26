#pragma once

// T8.9 档位菜单模型：把 DASH 目录（playurl 实际返回）变成一串可测试的菜单项。
//
// 边界（为什么模型不含“切档成功”）：
// - T8.9 只解析/列档，不做 DASH 双输入（T8.10）、不做档位策略（T8.12）。
// - 因此 DASH 档位在 T8.9 只能“列示 + 标注”，不能假装已切换/已可达。
// - 1080P60 / 4K 只手动：模型里标 `manual_only`；T8.9 不自动选，也不默认 4K。
// - 菜单文案不得把“可选”写成“保证可达”：4K 标注“可用档：性能/颜色不保证”，
//   未接入的档位标注“需 DASH 播放（T8.10）”。
//
// 本文件只依赖 Qt Core，便于单测（不需要 QApplication/QWidgets）。

#include <QString>
#include <QVector>

#include "online/PlayUrl.h"

namespace lapian::online {

enum class QualityMenuKind {
  kHeader = 0,  // 说明行（不可点）
  kTier,        // 一个 qn 档位
  kNote,        // 末尾提示（不可点）
};

struct QualityMenuEntry {
  QualityMenuKind kind = QualityMenuKind::kTier;
  QString text;
  QString tooltip;          // 可含带宽/init/index 范围；**不得含签名 URL**
  QString disabled_reason;  // 置灰原因（enabled=false 时非空）
  int qn = 0;
  int codecid = 0;
  bool checkable = false;
  bool checked = false;
  bool enabled = false;
  bool manual_only = false;  // 1080P60/720P60/4K/HDR/杜比/8K
};

struct QualityMenuContext {
  // 当前**播放**的档位（playurl 实际 qn/codecid，不是请求值）。
  int current_qn = 0;
  int current_codecid = 0;
  // 本次请求档；>0 且与 current 不同则菜单显式说明“实际以服务端为准”。
  int requested_qn = 0;
  int requested_codecid = 0;
  // 缓存命中：档位来自 meta，未联网。
  bool current_from_cache = false;
  // B：当前目录是否是“服务端拉回来的完整目录”（false = 缓存 meta 单档）。
  // 拿到服务端目录后必须改成 true，标题不能再谎称“未联网”。
  bool catalog_from_server = false;
  // B：用户点开菜单后正在按需拉完整 DASH 目录；菜单可打开并提示“正在获取”。
  bool catalog_loading = false;
  // T8.12：DASH 会话已可切档时为 true；缓存 meta 只有当前档时仍只列当前档。
  bool dash_playback_available = false;
  // 未登录时菜单明确写出“最高约 480P30（以实际返回为准）”。
  bool logged_in = true;
  // 实际流 fps（playurl dash_fps；缓存回退 meta）；只作展示，不参与档位判断。
  double actual_fps = 0.0;
  // 标题声明的源 fps（没有证据为 0）；仅用于“可能降帧”文案边界。
  double source_fps_hint = 0.0;
  // DASH 目录获取失败时的可读原因（不含 Cookie/URL）。
  QString error;
};

// 按 qn 降序生成：header → 各档位 → 必要的提示。
// 规则：
// - 当前播放档位 checked + enabled（点击只提示“已是当前档位”）；
// - 其它档位在 dash_playback_available=false 时置灰，原因写明 T8.10；
// - 1080P60/720P60/4K/HDR/Dolby/8K 标“仅手动”，4K 及以上再标“可用档：性能/颜色不保证”；
// - 目录为空时给一条可读说明（不伪造档位）。
QVector<QualityMenuEntry> BuildQualityMenuModel(const DashCatalog& catalog,
                                                const QualityMenuContext& context);

// 测试/证据用稳定文本（每行一项，不含 URL）：
//   [header] ...
//   [tier] qn=80 codec=7 checked=1 enabled=1 manual=0 text=...
//   [note] ...
QString QualityMenuModelText(const QVector<QualityMenuEntry>& entries);

}  // namespace lapian::online
