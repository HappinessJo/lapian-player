#pragma once

// SESSDATA 的本地规范化与安全校验（header-only，供 CredentialStore / BiliAuth /
// WinHttpClient 共用）。规则：
// - 允许用户粘贴裸值或整行 `SESSDATA=...`；去掉首尾空白/BOM；
// - 只接受单行、可打印 ASCII、长度 ≤1024；
// - 拒绝 CR/LF/`;`/`,`/`"`/`\`，防止 HTTP 头注入和跨 Cookie 注入。
// 这里不做任何网络请求，也不打印值。

#include <QChar>
#include <QString>

namespace lapian::online {

struct SessdataNormalization {
  bool ok = false;
  QString value;
  QString error;
};

inline bool IsSafeSessdataValue(const QString& value) {
  if (value.isEmpty() || value.size() > 1024) {
    return false;
  }
  for (const QChar ch : value) {
    const ushort code = ch.unicode();
    if (code < 0x21 || code > 0x7e) {
      return false;
    }
    if (ch == QLatin1Char(';') || ch == QLatin1Char(',') ||
        ch == QLatin1Char('"') || ch == QLatin1Char('\\')) {
      return false;
    }
  }
  return true;
}

inline SessdataNormalization NormalizeSessdata(const QString& raw) {
  SessdataNormalization result;
  QString value = raw;
  if (!value.isEmpty() && value.at(0) == QChar(0xfeff)) {
    value.remove(0, 1);
  }
  value = value.trimmed();
  if (value.startsWith(QStringLiteral("SESSDATA="), Qt::CaseInsensitive)) {
    value = value.mid(9).trimmed();
  }
  if (value.isEmpty()) {
    result.error = QStringLiteral("SESSDATA 为空");
    return result;
  }
  if (value.size() > 1024) {
    result.error = QStringLiteral("SESSDATA 过长（上限 1024 字符）");
    return result;
  }
  if (value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'))) {
    result.error = QStringLiteral("SESSDATA 必须是单行");
    return result;
  }
  if (!IsSafeSessdataValue(value)) {
    result.error = QStringLiteral("SESSDATA 含非法字符（只接受单行 ASCII）");
    return result;
  }
  result.ok = true;
  result.value = value;
  return result;
}

// 尽力清零内存中的明文。QString 可能共享缓冲区，这里只能清掉本对象持有的副本；
// 不把它当作密码学意义上的“内存擦除”承诺。
inline void ClearSecret(QString* value) {
  if (value == nullptr) {
    return;
  }
  value->fill(QChar(0));
  value->clear();
  value->squeeze();
}

}  // namespace lapian::online
