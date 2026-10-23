#include "online/QrCode.h"

#include <QPainter>
#include <QRect>

#include <algorithm>
#include <exception>
#include <optional>

#include "qrcodegen.hpp"

namespace lapian::online {
namespace {

QrImageResult Fail(const QString& error) {
  QrImageResult result;
  result.error = error;
  return result;
}

}  // namespace

QrImageResult RenderQrImage(const QString& text, int module_pixels,
                            int border_modules) {
  if (text.isEmpty()) {
    return Fail(QStringLiteral("二维码内容为空"));
  }
  const QByteArray utf8 = text.toUtf8();
  if (utf8.isEmpty() || utf8.size() > 4096) {
    return Fail(QStringLiteral("二维码内容过长（上限 4096 字节）"));
  }
  const int scale = std::clamp(module_pixels, 2, 20);
  const int border = std::clamp(border_modules, 0, 16);

  std::optional<qrcodegen::QrCode> encoded;
  try {
    encoded.emplace(qrcodegen::QrCode::encodeText(
        utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM));
  } catch (const std::exception& error) {
    return Fail(QStringLiteral("二维码编码失败：%1")
                    .arg(QString::fromUtf8(error.what())));
  }
  const qrcodegen::QrCode& code = *encoded;

  const int modules = code.getSize();
  const int image_modules = modules + border * 2;
  const qint64 image_pixels =
      static_cast<qint64>(image_modules) * static_cast<qint64>(scale);
  if (image_pixels <= 0 || image_pixels > 2048) {
    return Fail(QStringLiteral("二维码尺寸超出上限（%1 像素）").arg(image_pixels));
  }

  QImage image(static_cast<int>(image_pixels), static_cast<int>(image_pixels),
               QImage::Format_RGB32);
  if (image.isNull()) {
    return Fail(QStringLiteral("无法分配二维码图像"));
  }
  image.fill(Qt::white);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  for (int y = 0; y < modules; ++y) {
    for (int x = 0; x < modules; ++x) {
      if (!code.getModule(x, y)) {
        continue;
      }
      painter.fillRect(
          QRect((x + border) * scale, (y + border) * scale, scale, scale),
          Qt::black);
    }
  }
  painter.end();

  QrImageResult result;
  result.image = image;
  result.version = code.getVersion();
  result.modules = modules;
  result.module_pixels = scale;
  result.border_modules = border;
  return result;
}

}  // namespace lapian::online
