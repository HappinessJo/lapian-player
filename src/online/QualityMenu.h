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
  // 当前**播放**的档位（T8.9 = durl 实际 qn/codecid）。
  int current_qn = 0;
  int current_codecid = 0;
  // 缓存命中：档位来自 meta，未联网。
  bool current_from_cache = false;
  // T8.9 固定 false。T8.10/T8.12 接入 DASH 播放后由调用方置 true。
  bool dash_playback_available = false;
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
