#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

namespace lapian::core {

inline QString NormalizeAppDataRoot(QString root) {
  root = QDir::cleanPath(root);
  const QString duplicate = QStringLiteral("/拉片软件/拉片软件");
  if (root.endsWith(duplicate)) {
    root.chop(QStringLiteral("/拉片软件").size());
  }
  return root;
}

inline QString DefaultAppDataRoot() {
  QString root = qEnvironmentVariable("APPDATA");
  if (root.isEmpty()) {
    root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (root.isEmpty()) {
    root = QDir::homePath() + QStringLiteral("/AppData/Roaming");
  }
  return NormalizeAppDataRoot(root);
}

inline QString AppDataChild(const QString& root, const QString& child) {
  const QDir base(NormalizeAppDataRoot(root));
  if (QFileInfo(base.path()).fileName() == QStringLiteral("拉片软件")) {
    return base.filePath(child);
  }
  return base.filePath(QStringLiteral("拉片软件/") + child);
}

}  // namespace lapian::core
