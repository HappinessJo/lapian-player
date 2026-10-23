#pragma once

// T8.2.2 二维码本地渲染（vendored MIT：Project Nayuki QR-Code-generator）。
//
// - 纯本地：不加载网络图片/字体/统计；不引入 WebEngine/QtNetwork/新 DLL；
// - 高对比、整数缩放、4 模块 quiet zone；
// - 失败返回空图 + 可读错误（不抛异常）。

#include <QImage>
#include <QString>

namespace lapian::online {

struct QrImageResult {
  QImage image;
  QString error;
  int version = 0;         // QR version（1–40）
  int modules = 0;         // 不含 quiet zone 的模块数
  int module_pixels = 0;   // 每个模块的像素
  int border_modules = 0;  // quiet zone 模块数

  bool ok() const { return !image.isNull(); }
};

// text 为二维码内容（官方登录 URL）；module_pixels 会 clamp 到 2–20，
// border_modules 会 clamp 到 0–16（默认 4）。最长 4096 字节。
QrImageResult RenderQrImage(const QString& text, int module_pixels = 6,
                            int border_modules = 4);

}  // namespace lapian::online
