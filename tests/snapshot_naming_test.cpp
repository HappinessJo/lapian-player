// T5.4 截图命名/目录回退/旋转纯函数单测（不启动窗口）。

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSize>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <string>

#include "app/SnapshotImage.h"
#include "media/SnapshotNaming.h"

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckText(const QString& actual, const QString& expected, const std::string& what) {
  const bool ok = actual == expected;
  if (!ok) {
    std::printf("[FAIL] %s (actual=%s expected=%s)\n", what.c_str(),
                actual.toUtf8().constData(), expected.toUtf8().constData());
    ++g_failures;
  } else {
    std::printf("[ OK ] %s\n", what.c_str());
  }
}

void TestSanitize() {
  using lapian::snapshot::SanitizeSnapshotBaseName;
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("A")), QStringLiteral("A"),
            "普通文件名保持");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("测试")), QStringLiteral("测试"),
            "中文文件名保持");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral(R"(a<b>c:d"e/f\g|h?i*j)")),
            QStringLiteral("a_b_c_d_e_f_g_h_i_j"), "Windows 非法字符替换为 _");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("trail. ")),
            QStringLiteral("trail"), "去掉尾部点与空格");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("...")),
            QStringLiteral("snapshot"), "全点回退 snapshot");
  CheckText(SanitizeSnapshotBaseName(QString()), QStringLiteral("snapshot"),
            "空名回退 snapshot");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("CON")), QStringLiteral("_CON"),
            "保留设备名 CON 加前缀");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("con.final")),
            QStringLiteral("_con.final"), "保留设备名带扩展也加前缀");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("COM1")), QStringLiteral("_COM1"),
            "COM1 加前缀");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("COM10")),
            QStringLiteral("COM10"), "COM10 不是保留名");
  CheckText(SanitizeSnapshotBaseName(QStringLiteral("a\tb\nc")),
            QStringLiteral("a_b_c"), "控制字符替换");
  CheckText(SanitizeSnapshotBaseName(QString(150, QLatin1Char('a'))),
            QString(120, QLatin1Char('a')), "超长名截断到 120");
}

void TestTimecode() {
  using lapian::snapshot::FormatSnapshotTimecode;
  CheckText(FormatSnapshotTimecode(0, 24, 1), QStringLiteral("00-00-00-00"),
            "24fps 帧 0 时间码");
  CheckText(FormatSnapshotTimecode(100, 24, 1), QStringLiteral("00-00-04-04"),
            "24fps 帧 100 时间码");
  CheckText(FormatSnapshotTimecode(100, 30, 1), QStringLiteral("00-00-03-10"),
            "30fps 帧 100 时间码");
  CheckText(FormatSnapshotTimecode(100, 24000, 1001),
            QStringLiteral("00-00-04-04"), "23.976 按标称 24 时间码");
  CheckText(FormatSnapshotTimecode(100, 0, 0), QStringLiteral("00-00-03-10"),
            "无效帧率回退 30fps");
  CheckText(FormatSnapshotTimecode(30 * 60 * 60, 30, 1),
            QStringLiteral("01-00-00-00"), "超过 1 小时");
}

void TestNaming() {
  using lapian::snapshot::BuildSnapshotBaseName;
  using lapian::snapshot::SnapshotFileName;

  const QString base = BuildSnapshotBaseName(QStringLiteral("C:/videos/测试.mp4"),
                                             100, 24, 1);
  CheckText(base, QStringLiteral("测试_f100_00-00-04-04"), "截图基名含视频名/帧号/时间码");
  CheckText(SnapshotFileName(base, 0), base + QStringLiteral(".png"), "序号 0 无后缀");
  const QString one = SnapshotFileName(base, 1);
  const QString two = SnapshotFileName(base, 2);
  Check(one.endsWith(QStringLiteral("_1.png")), "序号 1 加 _1");
  Check(two.endsWith(QStringLiteral("_2.png")), "序号 2 加 _2");
  Check(one != two && one != SnapshotFileName(base, 0), "连续序号不重名");

  const QString weird =
      BuildSnapshotBaseName(QStringLiteral("C:/videos/A:B?.mov"), 0, 30, 1);
  Check(!weird.contains(QLatin1Char('?')) && !weird.contains(QLatin1Char(':')),
        "基名不含非法字符");
}

// UI v2 3C：截图网格扫历史目录时要能从文件名反解帧号（产品只写这些形态）。
void TestParseFrame() {
  using lapian::snapshot::ParseSnapshotFrameFromName;
  using lapian::snapshot::SnapshotFileName;

  const QString base =
      lapian::snapshot::BuildSnapshotBaseName(QStringLiteral("C:/videos/测试.mp4"),
                                              100, 30, 1);
  Check(ParseSnapshotFrameFromName(SnapshotFileName(base, 0)) == 100,
        "反解序号 0 的帧号");
  Check(ParseSnapshotFrameFromName(SnapshotFileName(base, 7)) == 100,
        "反解带重名序号的帧号");
  Check(ParseSnapshotFrameFromName(QStringLiteral("测试_f0_00-00-00-00.png")) == 0,
        "反解帧 0");
  Check(ParseSnapshotFrameFromName(QStringLiteral("测试_f1234567_11-22-33-04.png")) ==
            1234567,
        "反解 7 位帧号");
  Check(ParseSnapshotFrameFromName(QStringLiteral("测试_f42_00-00-01-12.PNG")) == 42,
        "扩展名大小写不敏感");
  // 视频名本身带 "_f123_00-00-00-00"：取最后一段（截图基名是最后追加的）。
  Check(ParseSnapshotFrameFromName(
            QStringLiteral("clip_f999_00-00-00-00_f5_00-00-00-05_2.png")) == 5,
        "取最后一段帧号");
  Check(ParseSnapshotFrameFromName(QStringLiteral("frame.png")) == -1,
        "非截图命名返回 -1");
  Check(ParseSnapshotFrameFromName(QStringLiteral("测试_f12_00-00-00.png")) == -1,
        "时间码位数不对返回 -1");
  Check(ParseSnapshotFrameFromName(QString()) == -1, "空名返回 -1");
}

void TestDirectoryFallback() {
  using lapian::snapshot::ResolveSnapshotDirectory;

  QTemporaryDir temp;
  Check(temp.isValid(), "创建临时目录");
  const QString videos = QDir(temp.path()).filePath(QStringLiteral("videos"));
  const QString pictures = QDir(temp.path()).filePath(QStringLiteral("pictures"));
  QDir().mkpath(videos);
  QDir().mkpath(pictures);
  const QString video = QDir(videos).filePath(QStringLiteral("测试.mp4"));
  QFile video_file(video);
  Check(video_file.open(QIODevice::WriteOnly), "创建测试视频占位");
  video_file.close();

  QString error;
  auto dir = ResolveSnapshotDirectory(video, pictures, &error);
  const QString primary = QDir(videos).filePath(QStringLiteral("snapshots"));
  CheckText(dir.path, primary, "默认使用视频目录 snapshots");
  Check(!dir.fallback, "默认不是回退");
  Check(QFileInfo(primary).isDir(), "snapshots 目录已创建");

  // snapshots 位置被同名文件占用（比 ACL 更容易稳定复现的不可写场景）。
  QDir(primary).removeRecursively();
  QFile blocker(primary);
  Check(blocker.open(QIODevice::WriteOnly), "用同名文件阻塞 snapshots");
  blocker.write("x");
  blocker.close();

  error.clear();
  dir = ResolveSnapshotDirectory(video, pictures, &error);
  const QString fallback = QDir(pictures).filePath(QStringLiteral("拉片软件"));
  CheckText(dir.path, fallback, "主目录不可写回退图片目录");
  Check(dir.fallback, "回退标记为 true");
  Check(!dir.reason.isEmpty(), "回退给出原因");
  Check(QFileInfo(fallback).isDir(), "图片目录已创建");

  // 回退目录也被文件占用：必须返回空路径 + 错误，而不是崩溃。
  QDir(fallback).removeRecursively();
  QFile fallback_blocker(fallback);
  Check(fallback_blocker.open(QIODevice::WriteOnly), "用同名文件阻塞图片目录");
  fallback_blocker.write("x");
  fallback_blocker.close();

  error.clear();
  dir = ResolveSnapshotDirectory(video, pictures, &error);
  Check(dir.path.isEmpty(), "两个目录都不可写时返回空路径");
  Check(!error.isEmpty(), "两个目录都不可写时给出错误");
}

void TestRotation() {
  using lapian::snapshot::NormalizeSnapshotRotation;
  using lapian::snapshot::RotateSnapshotImage;
  using lapian::snapshot::SnapshotDisplaySize;

  Check(NormalizeSnapshotRotation(90) == 90, "90° 保持");
  Check(NormalizeSnapshotRotation(-90) == 270, "负角度归一化");
  Check(NormalizeSnapshotRotation(450) == 90, "超过 360° 归一化");
  Check(NormalizeSnapshotRotation(44) == 0, "44° 吸附到 0");
  Check(NormalizeSnapshotRotation(45) == 90, "45° 吸附到 90");

  Check(SnapshotDisplaySize(QSize(1920, 1080), 0) == QSize(1920, 1080),
        "0° 输出尺寸不变");
  Check(SnapshotDisplaySize(QSize(1920, 1080), 90) == QSize(1080, 1920),
        "90° 交换宽高");
  Check(SnapshotDisplaySize(QSize(1920, 1080), 180) == QSize(1920, 1080),
        "180° 尺寸不变");
  Check(SnapshotDisplaySize(QSize(1920, 1080), 270) == QSize(1080, 1920),
        "270° 交换宽高");

  QImage source(2, 1, QImage::Format_RGB32);
  source.setPixelColor(0, 0, QColor(Qt::red));
  source.setPixelColor(1, 0, QColor(Qt::blue));

  const QImage ccw = RotateSnapshotImage(source, 90);
  Check(ccw.size() == QSize(1, 2), "90° 逆时针输出 1x2");
  Check(ccw.pixelColor(0, 0) == QColor(Qt::blue), "90° 逆时针后顶部为原右像素");
  Check(ccw.pixelColor(0, 1) == QColor(Qt::red), "90° 逆时针后底部为原左像素");

  const QImage cw = RotateSnapshotImage(source, 270);
  Check(cw.size() == QSize(1, 2), "270° 逆时针输出 1x2");
  Check(cw.pixelColor(0, 0) == QColor(Qt::red), "270° 逆时针后顶部为原左像素");
  Check(cw.pixelColor(0, 1) == QColor(Qt::blue), "270° 逆时针后底部为原右像素");

  const QImage half = RotateSnapshotImage(source, 180);
  Check(half.size() == QSize(2, 1), "180° 尺寸不变");
  Check(half.pixelColor(0, 0) == QColor(Qt::blue), "180° 首像素为原末像素");
  Check(half.pixelColor(1, 0) == QColor(Qt::red), "180° 末像素为原首像素");

  Check(RotateSnapshotImage(QImage(), 90).isNull(), "空图旋转仍为空");
}

}  // namespace

int main() {
  std::printf("T5.4 截图命名/目录/旋转测试\n");
  TestSanitize();
  TestTimecode();
  TestNaming();
  TestParseFrame();
  TestDirectoryFallback();
  TestRotation();

  if (g_failures > 0) {
    std::printf("\n失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
