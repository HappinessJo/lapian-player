#include "app/SnapshotImage.h"

#include <QTransform>

#include <cmath>

namespace lapian::snapshot {

int NormalizeSnapshotRotation(int degrees) {
  const int value = ((degrees % 360) + 360) % 360;
  const int snapped =
      static_cast<int>(std::lround(static_cast<double>(value) / 90.0)) * 90;
  return ((snapped % 360) + 360) % 360;
}

QSize SnapshotDisplaySize(const QSize& source_size,
                          int rotation_counterclockwise_degrees) {
  const int rotation = NormalizeSnapshotRotation(rotation_counterclockwise_degrees);
  if (rotation == 90 || rotation == 270) {
    return QSize(source_size.height(), source_size.width());
  }
  return source_size;
}

QImage RotateSnapshotImage(const QImage& source,
                           int rotation_counterclockwise_degrees) {
  const int rotation = NormalizeSnapshotRotation(rotation_counterclockwise_degrees);
  if (rotation == 0 || source.isNull()) {
    return source;
  }
  return source.transformed(QTransform().rotate(-rotation), Qt::FastTransformation);
}

}  // namespace lapian::snapshot
