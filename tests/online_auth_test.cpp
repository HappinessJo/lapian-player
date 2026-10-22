// T8.2 凭据/登录自检单测（不访问真实 B 站）：
// - SESSDATA 规范化/非法字符；
// - CredentialStore：DPAPI 往返、磁盘无明文、原子写、损坏/截断/目录回退、
//   清除后按匿名；
// - BiliAuth：nav 成功/-101/HTTP 412/429/解析错误/超时/取消，消息不含 sentinel；
// - BiliAuthSession：只在首次提交时检查、失败缓存、取消后可重试、清除回 anonymous；
// - WinHttpClient：Cookie 只允许 api.bilibili.com:443/HTTPS，CRLF 注入拒绝。

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "online/BiliAuth.h"
#include "online/CredentialStore.h"
#include "online/IHttpClient.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

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

bool Contains(const QString& text, const QString& term) {
  return text.contains(term, Qt::CaseInsensitive);
}

QByteArray NavOkJson() {
  return QByteArray(R"json({"code":0,"message":"OK","data":{"isLogin":true,
    "mid":123456,"uname":"normal-user","vipStatus":0,
    "wbi_img":{"img_url":"https://i0.hdslb.com/a.png","sub_url":"https://i0.hdslb.com/b.png"}}})json");
}

QByteArray Nav101Json() {
  return QByteArray(
      R"json({"code":-101,"message":"账号未登录","ttl":1,"data":{"isLogin":false}})json");
}

QByteArray NavNoLoginJson() {
  return QByteArray(
      R"json({"code":0,"message":"OK","data":{"isLogin":false,"vipStatus":0}})json");
}

QByteArray NavApiErrorJson() {
  return QByteArray(
      R"json({"code":62002,"message":"稿件不可见","data":{"isLogin":true}})json");
}

QString TestRoot() {
  return QDir::currentPath() + QStringLiteral("/build/t8.2-auth-test/") +
         QString::number(QCoreApplication::applicationPid());
}

void ResetDir(const QString& path) {
  QDir(path).removeRecursively();
  QDir().mkpath(path);
}

void TestNormalize() {
  using lapian::online::NormalizeSessdata;

  auto ok = [](const QString& input, const QString& expected, const char* what) {
    const auto result = NormalizeSessdata(input);
    const bool pass = result.ok && result.value == expected;
    Check(pass, what);
    if (!pass) {
      std::printf("  实际 ok=%d value_len=%lld\n", result.ok ? 1 : 0,
                  static_cast<long long>(result.value.size()));
    }
  };
  auto bad = [](const QString& input, const char* what) {
    const auto result = NormalizeSessdata(input);
    Check(!result.ok && !result.error.isEmpty(), what);
  };

  const QString sentinel = QStringLiteral("LAP_T82_SENTINEL_7f3c9a");
  ok(sentinel, sentinel, "裸 SESSDATA 通过");
  ok(QStringLiteral("  ") + sentinel + QStringLiteral("\r\n"), sentinel,
     "首尾空白/CRLF 被 trim 后通过");
  ok(QStringLiteral("SESSDATA=") + sentinel, sentinel,
     "SESSDATA= 前缀被剥离");
  ok(QStringLiteral("\uFEFF") + sentinel, sentinel, "UTF-8 BOM 被剥离");
  bad(QString(), "空 SESSDATA 拒绝");
  bad(QStringLiteral("a\nb"), "多行 SESSDATA 拒绝");
  bad(QStringLiteral("a;b"), "分号注入拒绝");
  bad(QStringLiteral("a,b"), "逗号拒绝");
  bad(QStringLiteral("a\"b"), "引号拒绝");
  bad(QStringLiteral("a\\b"), "反斜杠拒绝");
  bad(QStringLiteral("中文"), "非 ASCII 拒绝");
  bad(QString(1100, QLatin1Char('a')), "超长拒绝");
}

void TestCredentialStore() {
  using lapian::online::CredentialStore;

  const QString root = TestRoot();
  ResetDir(root);
  const QString path = root + QStringLiteral("/credentials.dat");
  const QString sentinel = QStringLiteral("LAP_T82_CRED_SENTINEL_4a7e21");

  {
    CredentialStore store(path);
    const auto missing = store.Load();
    Check(missing.missing && !missing.has_credential, "凭据缺失按无凭据处理");
    Check(!store.Has(), "Has() 对缺失文件为 false");

    QString error;
    Check(store.Save(sentinel, &error), "DPAPI 保存成功");
    Check(QFileInfo::exists(path), "credentials.dat 已写入");

    QFile raw(path);
    Check(raw.open(QIODevice::ReadOnly), "可读取 credentials.dat 做明文扫描");
    const QByteArray bytes = raw.readAll();
    raw.close();
    Check(bytes.contains("LAPCRED1"), "文件含 magic");
    Check(!bytes.contains(sentinel.toUtf8()), "磁盘无明文 SESSDATA");
    Check(!bytes.contains("SESSDATA="), "磁盘无 Cookie 头明文");

    const auto loaded = store.Load();
    Check(loaded.ok && loaded.has_credential, "DPAPI 解密成功");
    Check(loaded.sessdata == sentinel, "DPAPI 往返值一致");
    Check(store.Has(), "Has() 对有效文件为 true");

    const QStringList entries =
        QDir(root).entryList(QDir::Files | QDir::Hidden | QDir::System);
    Check(entries.size() == 1 && entries.first() == QStringLiteral("credentials.dat"),
          "原子写无残留临时文件");

    const QString second = QStringLiteral("LAP_T82_CRED_SECOND_9b1");
    Check(store.Save(QStringLiteral("SESSDATA=") + second), "覆盖保存成功");
    const auto reloaded = store.Load();
    Check(reloaded.ok && reloaded.sessdata == second, "覆盖后读到新值");

    QString clear_error;
    Check(store.Clear(&clear_error), "清除凭据成功");
    Check(!QFileInfo::exists(path), "清除后文件删除");
    Check(!store.Has(), "清除后 Has() 为 false");
    Check(store.Load().missing, "清除后 Load() 报 missing");
    Check(store.Clear(), "重复清除不报错");
  }

  {
    // 损坏：随机文本。
    const QString corrupt_path = root + QStringLiteral("/corrupt.dat");
    QFile file(corrupt_path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write("{ not a credential }");
    file.close();
    CredentialStore store(corrupt_path);
    const auto loaded = store.Load();
    Check(loaded.corrupt && !loaded.has_credential, "损坏文件按无凭据处理");
    Check(!store.Has(), "损坏文件 Has() 为 false");
    Check(!loaded.error.isEmpty(), "损坏文件给可读错误");
  }

  {
    // 截断：先保存，再砍掉末尾。
    const QString trunc_path = root + QStringLiteral("/truncated.dat");
    CredentialStore store(trunc_path);
    Check(store.Save(sentinel), "截断测试保存成功");
    QFile file(trunc_path);
    file.open(QIODevice::ReadWrite);
    file.resize(file.size() - 5);
    file.close();
    const auto loaded = store.Load();
    Check(loaded.corrupt && !loaded.has_credential, "截断文件按无凭据处理");
  }

  {
    // 错误 magic。
    const QString magic_path = root + QStringLiteral("/magic.dat");
    CredentialStore store(magic_path);
    Check(store.Save(sentinel), "magic 测试保存成功");
    QFile file(magic_path);
    file.open(QIODevice::ReadWrite);
    file.seek(0);
    file.write("BADMAGIC");
    file.close();
    const auto loaded = store.Load();
    Check(loaded.corrupt && !loaded.has_credential, "错误 magic 按无凭据处理");
  }

  {
    // 目录占用文件路径：保存失败但不崩。
    const QString dir_path = root + QStringLiteral("/as_directory");
    QDir().mkpath(dir_path);
    CredentialStore store(dir_path);
    QString error;
    Check(!store.Save(sentinel, &error), "路径是目录时保存失败");
    Check(!error.isEmpty(), "路径是目录时给可读错误");
    Check(store.Load().corrupt, "路径是目录时 Load 报损坏");
  }

  {
    // 非法值不落盘。
    const QString invalid_path = root + QStringLiteral("/invalid.dat");
    CredentialStore store(invalid_path);
    QString error;
    Check(!store.Save(QStringLiteral("bad;value"), &error), "非法 SESSDATA 拒绝保存");
    Check(!QFileInfo::exists(invalid_path), "非法值不创建文件");
    Check(!error.isEmpty(), "非法值给可读错误");
  }

  const QString default_path = lapian::online::DefaultCredentialPath();
  Check(QFileInfo(default_path).fileName() == QStringLiteral("credentials.dat"),
        "默认路径以 credentials.dat 结尾");
  Check(default_path.contains(QStringLiteral("拉片软件")),
        "默认路径含 拉片软件 目录");
}

class FakeHttpClient final : public lapian::online::IHttpClient {
 public:
  enum class Mode { kSuccess, kLogin101, kNoLogin, kApiError, kTimeout, kBlock };

  explicit FakeHttpClient(Mode mode) : mode_(mode) {}

  lapian::online::HttpResponse Get(
      const lapian::online::HttpRequest& request,
      const lapian::online::HttpCancelToken& cancel) override {
    ++calls;
    last_request = request;
    lapian::online::HttpResponse response;
    response.request_cookie_sent = !request.sessdata.isEmpty();
    if (mode_ == Mode::kBlock) {
      while (!cancel.IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      response.cancelled = true;
      response.error = QStringLiteral("请求已取消");
      return response;
    }
    if (mode_ == Mode::kTimeout) {
      response.timed_out = true;
      response.error = QStringLiteral("网络超时，请重试");
      return response;
    }
    response.ok = true;
    response.http_status = 200;
    switch (mode_) {
      case Mode::kSuccess:
        response.body = NavOkJson();
        break;
      case Mode::kLogin101:
        response.body = Nav101Json();
        break;
      case Mode::kNoLogin:
        response.body = NavNoLoginJson();
        break;
      case Mode::kApiError:
        response.body = NavApiErrorJson();
        break;
      case Mode::kTimeout:
      case Mode::kBlock:
        break;
    }
    return response;
  }

  int calls = 0;
  lapian::online::HttpRequest last_request;

 private:
  Mode mode_;
};

void CheckRedacted(const lapian::online::AuthResult& result,
                   const QString& sentinel, const char* what) {
  Check(!result.message.contains(sentinel) && !result.detail.contains(sentinel),
        what);
}

void TestBiliAuth() {
  using namespace lapian::online;

  const QString sentinel = QStringLiteral("LAP_T82_NAV_SENTINEL_55c1");

  {
    const AuthResult result = ParseNavJson(NavOkJson(), 200, 12);
    Check(result.ok() && result.state == AuthState::kLoggedIn, "nav code=0/isLogin=true 通过");
    Check(result.is_login && result.api_code == 0 && result.vip_status == 0,
          "nav 解析 isLogin/code/vipStatus");
    CheckRedacted(result, sentinel, "nav 成功消息不含 sentinel");
  }
  {
    const AuthResult result = ParseNavJson(Nav101Json(), 200, 12);
    Check(!result.ok() && result.state == AuthState::kLoginUnavailable &&
              result.api_code == -101,
          "nav -101 判为登录不可用");
    Check(Contains(result.message, QStringLiteral("登录不可用")) &&
              Contains(result.message, QStringLiteral("未登录")),
          "-101 提示明确按未登录播放");
    CheckRedacted(result, sentinel, "-101 消息不含 sentinel");
  }
  {
    const AuthResult result = ParseNavJson(NavNoLoginJson(), 200, 12);
    Check(!result.ok() && result.state == AuthState::kLoginUnavailable,
          "code=0 但 isLogin=false 判为登录不可用");
  }
  {
    const AuthResult result = ParseNavJson(NavApiErrorJson(), 200, 12);
    Check(!result.ok() && result.api_code == 62002 &&
              Contains(result.message, QStringLiteral("62002")),
          "其它 API code 给可读提示");
  }
  {
    const AuthResult result = ParseNavJson(QByteArray("{ broken"), 200, 5);
    Check(!result.ok() && result.error == AuthError::kParse, "坏 JSON 判为解析错误");
  }
  {
    const AuthResult result = ParseNavJson(QByteArray(), 412, 5);
    Check(!result.ok() && result.error == AuthError::kHttp &&
              Contains(result.message, QStringLiteral("412")) &&
              Contains(result.message, QStringLiteral("未登录")),
          "HTTP 412 给可读提示");
  }
  {
    const AuthResult result = ParseNavJson(QByteArray(), 429, 5);
    Check(!result.ok() && result.error == AuthError::kHttp &&
              Contains(result.message, QStringLiteral("429")),
          "HTTP 429 给可读提示");
  }

  {
    FakeHttpClient fake(FakeHttpClient::Mode::kSuccess);
    HttpCancelToken cancel;
    const AuthResult result = CheckNav(sentinel, &fake, cancel, 1500);
    Check(result.ok() && result.request_cookie_sent, "CheckNav 成功且带 Cookie");
    Check(fake.calls == 1, "CheckNav 只发 1 个请求");
    Check(fake.last_request.url == BuildNavUrl(), "CheckNav 使用固定 nav URL");
    Check(fake.last_request.sessdata == sentinel, "CheckNav 传显式 SESSDATA");
    Check(fake.last_request.timeout_ms == 1500, "CheckNav 使用传入超时");
    CheckRedacted(result, sentinel, "CheckNav 成功结果不含 sentinel");
  }
  {
    FakeHttpClient fake(FakeHttpClient::Mode::kLogin101);
    HttpCancelToken cancel;
    const AuthResult result = CheckNav(sentinel, &fake, cancel, 1500);
    Check(!result.ok() && result.state == AuthState::kLoginUnavailable,
          "CheckNav -101 失败");
    Check(result.request_cookie_sent, "-101 请求确实带了 Cookie");
    CheckRedacted(result, sentinel, "CheckNav -101 结果不含 sentinel");
  }
  {
    FakeHttpClient fake(FakeHttpClient::Mode::kTimeout);
    HttpCancelToken cancel;
    const AuthResult result = CheckNav(sentinel, &fake, cancel, 350);
    Check(!result.ok() && result.error == AuthError::kTimeout,
          "CheckNav 超时分类");
    Check(Contains(result.message, QStringLiteral("超时")) &&
              Contains(result.message, QStringLiteral("未登录")),
          "超时提示可读且不假装登录");
    CheckRedacted(result, sentinel, "超时结果不含 sentinel");
  }
  {
    FakeHttpClient fake(FakeHttpClient::Mode::kSuccess);
    HttpCancelToken cancel;
    const AuthResult result = CheckNav(QString(), &fake, cancel, 1500);
    Check(!result.ok() && result.error == AuthError::kNoCredential && fake.calls == 0,
          "空 SESSDATA 不发请求");
  }
  {
    FakeHttpClient fake(FakeHttpClient::Mode::kSuccess);
    HttpCancelToken cancel;
    const AuthResult result =
        CheckNav(QStringLiteral("bad\r\nX-Evil: 1"), &fake, cancel, 1500);
    Check(!result.ok() && result.error == AuthError::kInvalidCredential &&
              fake.calls == 0,
          "CRLF 注入的 SESSDATA 不发请求");
    CheckRedacted(result, QStringLiteral("X-Evil"), "注入值不出现在错误里");
  }
  {
    FakeHttpClient fake(FakeHttpClient::Mode::kBlock);
    HttpCancelToken cancel;
    AuthResult result;
    std::thread worker([&]() { result = CheckNav(sentinel, &fake, cancel, 5000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    QElapsedTimer timer;
    timer.start();
    cancel.Cancel();
    worker.join();
    Check(timer.elapsed() <= 100, "nav 取消 ≤100ms");
    Check(!result.ok() && result.error == AuthError::kCancelled, "nav 取消分类");
    Check(fake.calls == 1, "取消发生在请求中");
    CheckRedacted(result, sentinel, "取消结果不含 sentinel");
  }
}

void TestAuthSession() {
  using namespace lapian::online;

  BiliAuthSession session;
  Check(session.state() == AuthState::kAnonymous, "会话初始 anonymous");

  session.SetCredentialPresent(true);
  Check(session.state() == AuthState::kNotChecked, "有凭据 → not_checked");
  Check(session.BeginCheck(), "首次 BeginCheck 成功");
  Check(session.state() == AuthState::kChecking, "进入 checking");
  Check(!session.BeginCheck(), "检查中不重复 BeginCheck");

  AuthResult ok_result;
  ok_result.state = AuthState::kLoggedIn;
  ok_result.is_login = true;
  session.CompleteCheck(ok_result);
  Check(session.state() == AuthState::kLoggedIn, "成功 → logged_in");
  Check(!session.BeginCheck(), "已检查后本会话不再检查（DEC-3 缓存）");

  AuthResult fail_result;
  fail_result.state = AuthState::kLoginUnavailable;
  fail_result.api_code = -101;
  fail_result.message = QStringLiteral("登录不可用/凭据失效；将按未登录清晰度播放");
  session.SetCredentialPresent(true);  // 新凭据重新进入未检查
  Check(session.state() == AuthState::kNotChecked, "保存新凭据 → not_checked");
  Check(session.BeginCheck(), "新凭据可检查");
  session.CompleteCheck(fail_result);
  Check(session.state() == AuthState::kLoginUnavailable, "-101 → login_unavailable");
  Check(!session.BeginCheck(), "失败结果也缓存，不在每次提交重试");

  session.SetCredentialPresent(true);
  Check(session.BeginCheck(), "取消场景开始检查");
  session.CancelCheck();
  Check(session.state() == AuthState::kNotChecked, "取消 → not_checked 可重试");
  session.CompleteCheck(ok_result);
  Check(session.state() == AuthState::kNotChecked, "取消后的旧结果被忽略");

  session.Reset();
  Check(session.state() == AuthState::kAnonymous, "清除 → anonymous");
  Check(!session.has_credential(), "清除后 has_credential=false");
  Check(!session.BeginCheck(), "anonymous 不检查");
}

void TestWinHttpCookiePolicy() {
  using namespace lapian::online;

  Check(WinHttpClient::CookieAllowedForTest(
            QStringLiteral("https://api.bilibili.com/x/web-interface/nav")),
        "Cookie 允许 api.bilibili.com:443");
  Check(!WinHttpClient::CookieAllowedForTest(
            QStringLiteral("http://api.bilibili.com/x/web-interface/nav")),
        "Cookie 拒绝明文 HTTP");
  Check(!WinHttpClient::CookieAllowedForTest(
            QStringLiteral("https://api.bilibili.com:8443/x")),
        "Cookie 拒绝非 443 端口");
  Check(!WinHttpClient::CookieAllowedForTest(
            QStringLiteral("https://api.bilibili.com.evil.example/x")),
        "Cookie 拒绝相似域");
  Check(!WinHttpClient::CookieAllowedForTest(
            QStringLiteral("https://evil.example/x")),
        "Cookie 拒绝其它 host");

  const QString sentinel = QStringLiteral("LAP_T82_HTTP_SENTINEL_88ad");
  WinHttpClient client;
  HttpCancelToken cancel;

  {
    HttpRequest request;
    request.url = QStringLiteral("http://127.0.0.1:9/local");
    request.sessdata = sentinel;
    request.timeout_ms = 500;
    const HttpResponse response = client.Get(request, cancel);
    Check(!response.ok && response.error.contains(QStringLiteral("api.bilibili.com")),
          "非 API host 带 Cookie 被本地拒绝");
    Check(!response.request_cookie_sent, "拒绝时未标记 cookie_sent");
    Check(!response.error.contains(sentinel), "拒绝错误不含 sentinel");
  }
  {
    HttpRequest request;
    request.url = QStringLiteral("https://api.bilibili.com:8443/x");
    request.sessdata = sentinel;
    request.timeout_ms = 500;
    const HttpResponse response = client.Get(request, cancel);
    Check(!response.ok && response.error.contains(QStringLiteral("api.bilibili.com")),
          "非 443 端口带 Cookie 被本地拒绝");
  }
  {
    HttpRequest request;
    request.url = BuildNavUrl();
    request.sessdata = QStringLiteral("bad\r\nX-Evil: 1");
    request.timeout_ms = 500;
    const HttpResponse response = client.Get(request, cancel);
    Check(!response.ok && response.error.contains(QStringLiteral("凭据格式无效")),
          "CRLF 凭据在 WinHTTP 层拒绝");
    Check(!response.error.contains(QStringLiteral("X-Evil")),
          "WinHTTP 错误不含注入内容");
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);

  TestNormalize();
  TestCredentialStore();
  TestBiliAuth();
  TestAuthSession();
  TestWinHttpCookiePolicy();

  if (g_failures == 0) {
    std::printf("[OK] T8.2 online auth tests passed\n");
    return 0;
  }
  std::printf("[FAIL] T8.2 online auth tests failures=%d\n", g_failures);
  return 1;
}
