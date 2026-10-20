#pragma once

#include <QImage>
#include <QSize>

namespace lapian::snapshot {

// display matrix 的角度与渲染器保持一致：四舍五入到 90° 的倍数。
int NormalizeSnapshotRotation(int degrees);

// 按显示方向计算输出尺寸（90/270 交换宽高）。
QSize SnapshotDisplaySize(const QSize& source_size, int rotation_counterclockwise_degrees);

// 把源帧图像按 display matrix 旋转到显示方向。
// Qt 的 rotate() 在屏幕坐标（y 向下）里正角为顺时针，FFmpeg 的角度是逆时针，
// 所以这里传入 -rotation；0° 原样返回（共享缓冲，不拷贝）。
QImage RotateSnapshotImage(const QImage& source, int rotation_counterclockwise_degrees);

}  // namespace lapian::snapshot
