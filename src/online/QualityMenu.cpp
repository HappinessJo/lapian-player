#include "online/QualityMenu.h"

#include <QStringList>

namespace lapian::online {
namespace {

QString BandwidthText(qint64 bandwidth) {
  if (bandwidth <= 0) {
    return QString();
  }
  if (bandwidth >= 1000 * 1000) {
    return QStringLiteral("%1Mbps").arg(
        QString::number(static_cast<double>(bandwidth) / 1.0e6, 'f', 2));
  }
  return QStringLiteral("%1kbps").arg(bandwidth / 1000);
}

QString TierTooltip(const QualityTier& tier) {
  const DashStream* stream = tier.PreferredStream();
  QStringList parts;
  if (stream != nullptr) {
    parts.append(CodecName(stream->codecid));
    if (!stream->codecs.isEmpty()) {
      parts.append(stream->codecs);
    }
  }
  const QString bandwidth = BandwidthText(tier.bandwidth);
  if (!bandwidth.isEmpty()) {
    parts.append(bandwidth);
  }
  if (stream != nullptr) {
    if (stream->init_ok) {
      parts.append(QStringLiteral("init %1").arg(stream->init_range.ToString()));
      parts.append(QStringLiteral("index %1").arg(stream->index_range.ToString()));
    } else {
      parts.append(QStringLiteral("init/index 范围缺失"));
    }
    parts.append(QStringLiteral("备份 URL %1 个").arg(stream->backup_urls.size()));
  }
  return parts.join(QStringLiteral(" · "));
}

}  // namespace

QVector<QualityMenuEntry> BuildQualityMenuModel(const DashCatalog& catalog,
                                                const QualityMenuContext& context) {
  QVector<QualityMenuEntry> entries;

  QualityMenuEntry header;
  header.kind = QualityMenuKind::kHeader;
  // B：只有“缓存 meta 单档目录”才可以说未联网；拿到服务端目录后必须用服务端
  // 口径（目录多档时即便会话仍是缓存命中，也已经联网拉过目录）。
  const bool cache_meta_titles =
      context.current_from_cache && !context.catalog_from_server &&
      catalog.tiers.size() <= 1;
  header.text = cache_meta_titles
                    ? QStringLiteral("缓存命中档位（未联网；来自缓存 meta）")
                    : QStringLiteral("实际可用档位（以 playurl 返回为准）");
  entries.append(header);

  if (context.catalog_loading && catalog.tiers.size() <= 1) {
    QualityMenuEntry loading;
    loading.kind = QualityMenuKind::kNote;
    loading.text = QStringLiteral(
        "正在获取档位目录…（只拉目录，不启播/不换流；缓存播放不受影响）");
    entries.append(loading);
  }

  if (!catalog.HasVideo()) {
    QualityMenuEntry note;
    note.kind = QualityMenuKind::kNote;
    note.text =
        context.error.isEmpty()
            ? QStringLiteral("档位未获取：还没有 DASH 目录（不会伪造档位）")
            : QStringLiteral("档位获取失败：%1").arg(context.error);
    entries.append(note);
    if (!context.logged_in) {
      QualityMenuEntry login_note;
      login_note.kind = QualityMenuKind::kNote;
      login_note.text = QStringLiteral(
          "未登录：最高约 480P30（以实际返回为准）；登录不保证提升档位。");
      entries.append(login_note);
    }
    return entries;
  }

  if (context.current_qn > 0 && context.requested_qn > 0 &&
      context.current_qn != context.requested_qn) {
    QualityMenuEntry note;
    note.kind = QualityMenuKind::kNote;
    note.text = QStringLiteral(
                    "请求 %1（qn=%2），服务端实际返回 %3（qn=%4）；以实际为准")
                    .arg(QualityName(context.requested_qn))
                    .arg(context.requested_qn)
                    .arg(QualityName(context.current_qn))
                    .arg(context.current_qn);
    entries.append(note);
  }

  bool current_found = false;
  for (const QualityTier& tier : catalog.tiers) {
    QualityMenuEntry entry;
    entry.kind = QualityMenuKind::kTier;
    entry.qn = tier.qn;
    entry.checkable = true;
    entry.manual_only = IsManualOnlyTier(tier);
    const DashStream* stream = tier.PreferredStream();
    entry.codecid = stream != nullptr ? stream->codecid : 0;
    entry.text = QualityTierLabel(tier);
    entry.tooltip = TierTooltip(tier);
    if ((tier.qn == 116 || tier.qn == 74) && !tier.has_avc()) {
      // HEVC/AV1-only 60fps：只进手动菜单，绝不自动、不静默降帧。
      entry.text += QStringLiteral("（60fps / %1（手动））")
                        .arg(stream != nullptr ? CodecName(stream->codecid)
                                               : QStringLiteral("HEVC/AV1"));
    } else if (tier.qn == 116 || tier.qn == 74) {
      entry.text += QStringLiteral("（60fps / AVC · 自动优先）");
    }
    if (tier.qn >= 120) {
      // 4K/HDR/杜比/8K 是“可用档”，不是 MVP 承诺。
      entry.text += QStringLiteral("（仅手动 · 可用档：性能/颜色不保证）");
    }
    entry.checked =
        context.current_qn == tier.qn &&
        (context.current_codecid <= 0 || context.current_codecid == entry.codecid);
    if (entry.checked) {
      entry.text += QStringLiteral("（当前播放）");
      entry.enabled = true;
      entry.tooltip = QStringLiteral("%1 · 已经是当前播放档位")
                          .arg(entry.tooltip.isEmpty()
                                   ? QStringLiteral("当前档位")
                                   : entry.tooltip);
      current_found = true;
    } else if (!context.dash_playback_available) {
      entry.enabled = false;
      entry.disabled_reason =
          entry.manual_only
              ? QStringLiteral("只允许手动选择；当前会话还不能重新取流")
              : QStringLiteral("当前会话还不能重新取流（未就绪或缓存只含当前档）");
      entry.text += QStringLiteral("（当前不可切换）");
    } else {
      entry.enabled = true;
      entry.tooltip = entry.manual_only
                          ? QStringLiteral("仅手动切换（不自动选；重新取流）")
                          : QStringLiteral("手动切换档位（重新取流/重建索引）");
    }
    entries.append(entry);
  }

  if (context.current_qn > 0 && !current_found) {
    QualityMenuEntry note;
    note.kind = QualityMenuKind::kNote;
    note.text = QStringLiteral("当前播放 %1 不在 DASH 目录里（durl 与服务端档位不同）")
                    .arg(QualityName(context.current_qn));
    entries.append(note);
  }
  if (!context.error.isEmpty()) {
    // B：显式拉目录失败不清掉已有目录（缓存播放/当前档不受影响）；菜单里
    // 明确写出失败原因，不静默换档。
    QualityMenuEntry note;
    note.kind = QualityMenuKind::kNote;
    note.text = QStringLiteral("档位目录刷新失败：%1（保留上次目录；缓存播放不受影响）")
                    .arg(context.error);
    entries.append(note);
  }
  if (!context.logged_in) {
    QualityMenuEntry note;
    note.kind = QualityMenuKind::kNote;
    // 未登录时匿名目录实测只到 480P；如果服务端意外给了更高档，仍以实际为准。
    note.text = catalog.MaxQn() <= 32
                    ? QStringLiteral("未登录：最高约 480P30（以实际返回为准）；"
                                     "登录不保证提升档位。")
                    : QStringLiteral("未登录：本次实际最高 %1；"
                                     "以实际返回为准，登录不保证提升档位。")
                          .arg(QualityName(catalog.MaxQn()));
    entries.append(note);
  }
  {
    QualityMenuEntry note;
    note.kind = QualityMenuKind::kNote;
    note.text = QStringLiteral(
        "菜单只列 playurl 实际返回的档位；菜单可选不等于保证可达。");
    entries.append(note);
  }
  if (!catalog.HasAudio()) {
    QualityMenuEntry note;
    note.kind = QualityMenuKind::kNote;
    note.text = QStringLiteral("DASH 目录没有音频流（无音轨或响应缺 dash.audio）");
    entries.append(note);
  }
  return entries;
}

QString QualityMenuModelText(const QVector<QualityMenuEntry>& entries) {
  QStringList lines;
  for (const QualityMenuEntry& entry : entries) {
    QString prefix;
    switch (entry.kind) {
      case QualityMenuKind::kHeader:
        prefix = QStringLiteral("[header]");
        break;
      case QualityMenuKind::kNote:
        prefix = QStringLiteral("[note]");
        break;
      case QualityMenuKind::kTier:
        prefix = QStringLiteral("[tier] qn=%1 codec=%2 checked=%3 enabled=%4 "
                                "manual=%5 text=%6")
                     .arg(entry.qn)
                     .arg(entry.codecid)
                     .arg(entry.checked ? 1 : 0)
                     .arg(entry.enabled ? 1 : 0)
                     .arg(entry.manual_only ? 1 : 0)
                     .arg(entry.text);
        break;
    }
    if (entry.kind != QualityMenuKind::kTier) {
      prefix += QStringLiteral(" ") + entry.text;
    }
    if (!entry.disabled_reason.isEmpty()) {
      prefix += QStringLiteral(" reason=") + entry.disabled_reason;
    }
    lines.append(prefix);
  }
  return lines.join(QLatin1Char('\n'));
}

}  // namespace lapian::online
