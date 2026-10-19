// 零依赖冒烟测试：T0.1 只验证工具链与 CTest 通路。
// Catch2 推迟到 T1.1（需要真实帧数学单测时）再引入，避免构建期网络成为
// T0.1 的阻塞项；这里的断言足够覆盖“Qt 链接正确、版本号注入正确”。

#include <cstdio>

#include <QString>
#include <QtGlobal>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what);
  } else {
    std::printf("[FAIL] %s\n", what);
    ++g_failures;
  }
}

}  // namespace

int main() {
  Check(QT_VERSION_MAJOR == 6, "Qt 主版本为 6");
  Check(QT_VERSION_MINOR >= 8, "Qt 次版本 >= 8");

  const QString version = QStringLiteral(LAPIAN_VERSION);
  Check(!version.isEmpty(), "应用版本号非空");
  Check(version.count(QLatin1Char('.')) >= 1, "应用版本号含点号");

  std::printf("Qt 运行版本: %s\n", qVersion());
  return g_failures == 0 ? 0 : 1;
}
