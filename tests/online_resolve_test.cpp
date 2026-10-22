// T8.1 解析层单测：
// - BV/URL 归一化、p=2、非法/空串；
// - view JSON（标题/UP/时长/分P/大 cid/错误码/缺字段）；
// - BiliResolveClient 的“非法不发请求”、URL 构造、错误映射、取消；
// - WinHttpClient 的真实本地慢服务器：成功、超时、取消 ≤100ms、无 Cookie 头。

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "online/BiliResolveClient.h"
#include "online/BiliResolver.h"
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

QByteArray Ob1Json() {
  return QByteArray(R"json({
    "code": 0,
    "message": "OK",
    "data": {
      "bvid": "BV1BMnv6jE5S",
      "aid": 117476968894603,
      "title": "在超市后门吸烟的二人【静止系MAD】",
      "owner": {"name": "忆雪猫"},
      "duration": 111,
      "pages": [
        {"page": 1, "cid": 42915922022, "part": "在超市后门吸烟的二人【静止系MAD】", "duration": 111}
      ]
    }
  })json");
}

QByteArray MultiPJson() {
  return QByteArray(R"json({
    "code": 0,
    "message": "OK",
    "data": {
      "bvid": "BV1fNnv6EEon",
      "aid": 1,
      "title": "《代号：神不言》UFSH2026参会实机技术演示&实机特别影像",
      "owner": {"name": "代号神不言"},
      "duration": 501,
      "pages": [
        {"page": 1, "cid": 42915136857, "part": "P1 中文演示", "duration": 110},
        {"page": 2, "cid": 42915138952, "part": "P2 中文特别影像", "duration": 57},
        {"page": 3, "cid": 42915201367, "part": "P3 日文演示", "duration": 110},
        {"page": 4, "cid": 42915203356, "part": "P4 日文特别影像", "duration": 57},
        {"page": 5, "cid": 42915204226, "part": "P5 英文演示", "duration": 110},
        {"page": 6, "cid": 42915267806, "part": "P6 英文特别影像", "duration": 57}
      ]
    }
  })json");
}

QByteArray ErrorJson(int code, const char* message) {
  return QByteArray("{\"code\":") + QByteArray::number(code) +
         QByteArray(",\"message\":\"") + QByteArray(message) + QByteArray("\"}");
}

void TestNormalize() {
  using lapian::online::NormalizeInput;

  auto ok_case = [](const QString& input, const QString& bvid, int page,
                    bool page_from_input, const char* what) {
    const auto result = NormalizeInput(input);
    const bool ok = result.ok && result.bvid == bvid && result.page == page &&
                    result.page_from_input == page_from_input;
    if (ok) {
      std::printf("[ OK ] %s\n", what);
    } else {
      std::printf("[FAIL] %s\n  实际：ok=%d bvid=%s page=%d from_input=%d\n",
                  what, result.ok ? 1 : 0, result.bvid.toUtf8().constData(),
                  result.page, result.page_from_input ? 1 : 0);
      ++g_failures;
    }
  };

  auto bad_case = [](const QString& input, const QString& term,
                     const char* what) {
    const auto result = NormalizeInput(input);
    const bool ok = !result.ok && result.error.message.contains(term);
    if (ok) {
      std::printf("[ OK ] %s\n", what);
    } else {
      std::printf("[FAIL] %s\n  实际：ok=%d error=%s\n", what, result.ok ? 1 : 0,
                  result.error.message.toUtf8().constData());
      ++g_failures;
    }
  };

  ok_case(QStringLiteral("BV1BMnv6jE5S"), QStringLiteral("BV1BMnv6jE5S"), 1,
          false, "裸 BV");
  ok_case(QStringLiteral("bv1BMnv6jE5S"), QStringLiteral("BV1BMnv6jE5S"), 1,
          false, "小写 bv 前缀归一化");
  ok_case(QStringLiteral("https://www.bilibili.com/video/BV1BMnv6jE5S"),
          QStringLiteral("BV1BMnv6jE5S"), 1, false, "标准视频 URL");
  ok_case(QStringLiteral("https://www.bilibili.com/video/BV1BMnv6jE5S?p=2"),
          QStringLiteral("BV1BMnv6jE5S"), 2, true, "URL ?p=2");
  ok_case(QStringLiteral(
              "https://www.bilibili.com/video/BV1GJ411x7h7/?p=3&spm_id_from=333.999"),
          QStringLiteral("BV1GJ411x7h7"), 3, true, "URL 尾斜杠 + 额外参数");
  ok_case(QStringLiteral("www.bilibili.com/video/BV1BMnv6jE5S?p=2"),
          QStringLiteral("BV1BMnv6jE5S"), 2, true, "无 scheme 的 URL");
  ok_case(QStringLiteral("//www.bilibili.com/video/BV1BMnv6jE5S?p=2"),
          QStringLiteral("BV1BMnv6jE5S"), 2, true, "协议相对 URL");
  ok_case(QStringLiteral("http://www.bilibili.com/video/BV1BMnv6jE5S?p=2"),
          QStringLiteral("BV1BMnv6jE5S"), 2, true, "http URL 归一化为请求 https");

  bad_case(QString(), QStringLiteral("请输入"), "空串本地拒绝");
  bad_case(QStringLiteral("   "), QStringLiteral("请输入"), "空白本地拒绝");
  bad_case(QStringLiteral("BV1"), QStringLiteral("12 位"), "BV1 过短");
  bad_case(QStringLiteral("BV1BMnv6jE5"), QStringLiteral("12 位"), "少一位");
  bad_case(QStringLiteral("BV1BMnv6jE5SS"), QStringLiteral("12 位"), "多一位");
  bad_case(QStringLiteral("BV1BMnv6jE5!"), QStringLiteral("非法字符"), "非法字符");
  bad_case(QStringLiteral("av170001"), QStringLiteral("av"), "av 号明确拒绝");
  bad_case(QStringLiteral("https://www.bilibili.com/video/av170001"),
           QStringLiteral("av"), "URL av 号明确拒绝");
  bad_case(QStringLiteral("b23.tv/abc"), QStringLiteral("短链"), "短链明确拒绝");
  bad_case(QStringLiteral("https://b23.tv/abc"), QStringLiteral("短链"),
           "https 短链明确拒绝");
  bad_case(QStringLiteral("https://example.com/video/BV1BMnv6jE5S"),
           QStringLiteral("bilibili.com"), "非 B 站 host 拒绝");
  bad_case(QStringLiteral("https://www.bilibili.com/space/123"),
           QStringLiteral("BV"), "非视频页拒绝");
  bad_case(QStringLiteral("https://www.bilibili.com/video/BV1BMnv6jE5S?p=0"),
           QStringLiteral("正整数"), "p=0 拒绝");
  bad_case(QStringLiteral("https://www.bilibili.com/video/BV1BMnv6jE5S?p=abc"),
           QStringLiteral("正整数"), "p=abc 拒绝");
  bad_case(QStringLiteral("随便搜索一个视频"), QStringLiteral("搜索词"),
           "搜索词拒绝");
  bad_case(QStringLiteral("BV1BMnv6jE5S?p=2"), QStringLiteral("不支持"),
           "裸 BV 带 query 明确拒绝");

  Check(lapian::online::BuildViewUrl(QStringLiteral("BV1BMnv6jE5S")) ==
            QStringLiteral(
                "https://api.bilibili.com/x/web-interface/view?bvid=BV1BMnv6jE5S"),
        "view URL 固定为 api.bilibili.com 匿名地址");
}

void TestParseView() {
  using namespace lapian::online;

  BiliVideo video;
  BiliResolveError error = ParseViewJson(Ob1Json(), &video);
  Check(error.ok(), "OB-1 JSON 解析成功");
  Check(video.bvid == QStringLiteral("BV1BMnv6jE5S"), "view bvid");
  Check(video.title.contains(QStringLiteral("在超市后门吸烟的二人")), "view 标题");
  Check(video.owner == QStringLiteral("忆雪猫"), "view UP 主");
  Check(video.duration_seconds == 111, "view 总时长 111s");
  Check(video.pages.size() == 1, "view 分P 数量");
  Check(video.pages.at(0).cid == 42915922022LL, "view 大 cid（>2^31）");
  Check(video.pages.at(0).duration_seconds == 111, "view 分P 时长");
  Check(video.selected_index == 0, "默认 P1");

  BiliVideo multi;
  error = ParseViewJson(MultiPJson(), &multi);
  Check(error.ok() && multi.pages.size() == 6, "多P JSON 6 个分P");
  error = SelectPage(&multi, 2);
  Check(error.ok() && multi.selected_index == 1, "SelectPage(2) 选数组下标 1");
  Check(multi.selected_page() != nullptr &&
            multi.selected_page()->cid == 42915138952LL &&
            multi.selected_page()->duration_seconds == 57,
        "P2 cid/时长正确");
  error = SelectPage(&multi, 3);
  Check(error.ok() && multi.selected_index == 2 &&
            multi.selected_page()->cid == 42915201367LL,
        "SelectPage(3) cid 正确");
  error = SelectPage(&multi, 99);
  Check(!error.ok() && error.message.contains(QStringLiteral("P99")),
        "分P 越界可读错误");
  error = SelectPage(&multi, 0);
  Check(!error.ok(), "分P 0 拒绝");

  auto api_error = [](int code, const char* message, const QString& term,
                      const char* what) {
    BiliVideo ignored;
    const BiliResolveError result =
        ParseViewJson(ErrorJson(code, message), &ignored);
    Check(result.kind == BiliErrorKind::kApi && result.api_code == code &&
              result.message.contains(term),
          what);
  };
  api_error(-400, "请求错误", QStringLiteral("参数错误"), "-400 映射");
  api_error(-404, "啥都木有", QStringLiteral("不存在"), "-404 映射");
  api_error(62002, "稿件不可见", QStringLiteral("不可见"), "62002 映射");
  api_error(-101, "账号未登录", QStringLiteral("登录不可用"), "-101 映射");
  api_error(12345, "未知错误", QStringLiteral("错误码 12345"), "未知码通用提示");

  BiliVideo ignored;
  Check(!ParseViewJson(QByteArray("{bad"), &ignored).ok(), "坏 JSON 拒绝");
  Check(!ParseViewJson(QByteArray("{}"), &ignored).ok(), "缺 code 拒绝");
  Check(!ParseViewJson(ErrorJson(0, "OK"), &ignored).ok(), "code=0 但缺 data 拒绝");
  Check(!ParseViewJson(QByteArray(R"json({"code":0,"data":{"bvid":"BV1BMnv6jE5S","title":"x","owner":{"name":"u"},"duration":1,"pages":[]}})json"),
                       &ignored)
             .ok(),
        "空分P 拒绝");
}

class FakeHttpClient final : public lapian::online::IHttpClient {
 public:
  lapian::online::HttpResponse response;
  lapian::online::HttpRequest last_request;
  int calls = 0;

  lapian::online::HttpResponse Get(
      const lapian::online::HttpRequest& request,
      const lapian::online::HttpCancelToken&) override {
    ++calls;
    last_request = request;
    return response;
  }
};

class BlockingHttpClient final : public lapian::online::IHttpClient {
 public:
  std::atomic<int> calls{0};

  lapian::online::HttpResponse Get(
      const lapian::online::HttpRequest&,
      const lapian::online::HttpCancelToken& cancel) override {
    calls.fetch_add(1);
    lapian::online::HttpResponse response;
    for (int i = 0; i < 2000; ++i) {
      if (cancel.IsCancelled()) {
        response.cancelled = true;
        response.error = QStringLiteral("请求已取消");
        return response;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    response.ok = true;
    response.http_status = 200;
    response.body = Ob1Json();
    return response;
  }
};

void TestResolveClient() {
  using namespace lapian::online;

  FakeHttpClient fake;
  BiliResolveClient client(&fake);
  HttpCancelToken token;
  auto result = client.Resolve(QStringLiteral("BV1"), token);
  Check(!result.error.ok() && result.error.kind == BiliErrorKind::kInvalidInput,
        "非法输入返回 invalid-input");
  Check(fake.calls == 0 && client.request_count() == 0,
        "非法输入 0 HTTP 请求（不发网络）");

  fake.response.ok = true;
  fake.response.http_status = 200;
  fake.response.body = Ob1Json();
  result = client.Resolve(QStringLiteral("BV1BMnv6jE5S"), token);
  Check(result.error.ok() && result.video.title.contains(
                                      QStringLiteral("在超市后门吸烟的二人")),
        "成功解析 view");
  Check(fake.calls == 1 && client.request_count() == 1, "成功时恰好 1 次请求");
  Check(fake.last_request.url ==
            QStringLiteral(
                "https://api.bilibili.com/x/web-interface/view?bvid=BV1BMnv6jE5S"),
        "请求 URL 只含 bvid");
  Check(fake.last_request.url.contains(QStringLiteral("api.bilibili.com")) &&
            !fake.last_request.url.contains(QStringLiteral("Cookie")) &&
            !fake.last_request.url.contains(QStringLiteral("SESSDATA")),
        "请求 URL 不含 Cookie/SESSDATA");

  fake.response.body = MultiPJson();
  result = client.Resolve(
      QStringLiteral("https://www.bilibili.com/video/BV1fNnv6EEon?p=2"), token);
  Check(result.error.ok() && result.video.selected_index == 1 &&
            result.video.selected_page()->cid == 42915138952LL,
        "URL ?p=2 选到 P2");

  fake.response = HttpResponse();
  fake.response.ok = true;  // HTTP 200 + 业务 code 非 0 的真实组合
  fake.response.http_status = 200;
  fake.response.body = ErrorJson(-404, "啥都木有");
  result = client.Resolve(QStringLiteral("BV1zz411c7mD"), token);
  Check(!result.error.ok() && result.error.kind == BiliErrorKind::kApi &&
            result.error.api_code == -404,
        "API -404 映射");

  fake.response = HttpResponse();
  fake.response.ok = false;
  fake.response.timed_out = true;
  fake.response.error = QStringLiteral("网络超时，请重试");
  result = client.Resolve(QStringLiteral("BV1BMnv6jE5S"), token);
  Check(!result.error.ok() && result.error.kind == BiliErrorKind::kTimeout,
        "超时映射");

  fake.response = HttpResponse();
  fake.response.ok = false;
  fake.response.http_status = 429;
  result = client.Resolve(QStringLiteral("BV1BMnv6jE5S"), token);
  Check(!result.error.ok() && result.error.kind == BiliErrorKind::kHttp &&
            result.error.message.contains(QStringLiteral("拒绝")),
        "HTTP 429 映射");

  fake.response = HttpResponse();
  fake.response.ok = false;
  fake.response.http_status = 500;
  result = client.Resolve(QStringLiteral("BV1BMnv6jE5S"), token);
  Check(!result.error.ok() && result.error.kind == BiliErrorKind::kHttp &&
            result.error.message.contains(QStringLiteral("HTTP 500")),
        "HTTP 500 映射");

  HttpCancelToken cancelled_token;
  cancelled_token.Cancel();
  FakeHttpClient cancelled_fake;
  BiliResolveClient cancelled_client(&cancelled_fake);
  result = cancelled_client.Resolve(QStringLiteral("BV1BMnv6jE5S"),
                                    cancelled_token);
  Check(!result.error.ok() && result.error.kind == BiliErrorKind::kCancelled &&
            cancelled_fake.calls == 0,
        "取消在请求前生效，0 请求");

  BlockingHttpClient blocking;
  BiliResolveClient blocking_client(&blocking);
  HttpCancelToken blocking_token;
  BiliResolveResult blocking_result;
  std::thread worker([&]() {
    blocking_result =
        blocking_client.Resolve(QStringLiteral("BV1BMnv6jE5S"), blocking_token);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  QElapsedTimer cancel_timer;
  cancel_timer.start();
  blocking_token.Cancel();
  worker.join();
  const qint64 cancel_ms = cancel_timer.elapsed();
  Check(blocking.calls.load() == 1, "取消发生在请求中");
  Check(!blocking_result.error.ok() &&
            blocking_result.error.kind == BiliErrorKind::kCancelled,
        "请求中取消返回 cancelled");
  Check(cancel_ms <= 100, "BiliResolveClient 取消 ≤100ms");
  std::printf("T8.1 unit resolve_client_cancel_ms=%lld\n",
              static_cast<long long>(cancel_ms));
}

class SlowServer {
 public:
  explicit SlowServer(int delay_ms) : delay_ms_(delay_ms) {
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) {
      return;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
        0) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
      return;
    }
    sockaddr_in bound{};
    int bound_length = sizeof(bound);
    if (getsockname(listener_, reinterpret_cast<sockaddr*>(&bound),
                    &bound_length) != 0) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
      return;
    }
    port_ = ntohs(bound.sin_port);
    if (listen(listener_, 4) != 0) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
      return;
    }
    thread_ = std::thread([this]() { Run(); });
  }

  ~SlowServer() {
    stop_.store(true);
    if (listener_ != INVALID_SOCKET) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  bool ok() const { return port_ > 0; }
  int port() const { return port_; }
  QString url(const QString& path = QStringLiteral("/view")) const {
    return QStringLiteral("http://127.0.0.1:%1%2").arg(port_).arg(path);
  }
  std::string last_request() const {
    std::lock_guard<std::mutex> lock(request_mutex_);
    return last_request_;
  }

 private:
  void Run() {
    const SOCKET client = accept(listener_, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
      return;
    }
    char buffer[4096];
    const int received = recv(client, buffer, sizeof(buffer), 0);
    if (received > 0) {
      std::lock_guard<std::mutex> lock(request_mutex_);
      last_request_.assign(buffer, static_cast<std::size_t>(received));
    }
    for (int elapsed = 0; elapsed < delay_ms_ && !stop_.load(); elapsed += 10) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!stop_.load()) {
      const std::string body = "{\"code\":0,\"message\":\"OK\"}";
      std::string response =
          "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
          "Content-Length: " +
          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      int sent = 0;
      while (sent < static_cast<int>(response.size())) {
        const int n = send(client, response.data() + sent,
                           static_cast<int>(response.size()) - sent, 0);
        if (n <= 0) {
          break;
        }
        sent += n;
      }
    }
    closesocket(client);
  }

  SOCKET listener_ = INVALID_SOCKET;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  int delay_ms_ = 0;
  int port_ = 0;
  mutable std::mutex request_mutex_;
  std::string last_request_;
};

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

void TestWinHttpClient() {
  using namespace lapian::online;

  Check(!WinHttpClient::LibraryLoadedForTest(), "构造/未解析前未加载 winhttp.dll");
  Check(WinHttpClient::ValidateUrlForTest(
            QStringLiteral("https://api.bilibili.com/x/web-interface/view?bvid=x"))
            .isEmpty(),
        "https URL 校验通过");
  Check(!WinHttpClient::ValidateUrlForTest(QStringLiteral("ftp://example.com/x"))
             .isEmpty(),
        "非 http(s) URL 校验失败");

  {
    SlowServer server(0);
    Check(server.ok(), "本地慢服务器启动");
    if (server.ok()) {
      WinHttpClient client;
      HttpCancelToken token;
      HttpRequest request;
      request.url = server.url();
      request.timeout_ms = 3000;
      const HttpResponse response = client.Get(request, token);
      Check(response.ok && response.http_status == 200, "WinHTTP 本地 200");
      Check(response.body.contains("\"code\":0"), "WinHTTP 响应体");
      Check(!response.cancelled && !response.timed_out, "成功响应非取消/超时");
      const std::string raw = Lower(server.last_request());
      Check(raw.find("cookie:") == std::string::npos &&
                raw.find("sessdata") == std::string::npos,
            "请求不带 Cookie/SESSDATA 头");
      Check(WinHttpClient::LibraryLoadedForTest(), "首次解析后 winhttp.dll 已加载");
    }
  }

  {
    SlowServer server(3000);
    Check(server.ok(), "超时服务器启动");
    if (server.ok()) {
      WinHttpClient client;
      HttpCancelToken token;
      HttpRequest request;
      request.url = server.url();
      request.timeout_ms = 400;
      const HttpResponse response = client.Get(request, token);
      Check(response.timed_out && !response.ok, "WinHTTP 超时标记");
      Check(response.elapsed_ms >= 300 && response.elapsed_ms <= 1600,
            "WinHTTP 超时在 400ms 量级返回");
      std::printf("T8.1 unit winhttp_timeout_elapsed_ms=%lld\n",
                  static_cast<long long>(response.elapsed_ms));
    }
  }

  {
    SlowServer server(5000);
    Check(server.ok(), "取消服务器启动");
    if (server.ok()) {
      WinHttpClient client;
      HttpCancelToken token;
      HttpRequest request;
      request.url = server.url();
      request.timeout_ms = 8000;
      HttpResponse response;
      std::atomic<bool> finished{false};
      std::thread worker([&]() {
        response = client.Get(request, token);
        finished.store(true);
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      Check(!finished.load(), "取消前请求仍在等待");
      QElapsedTimer timer;
      timer.start();
      token.Cancel();
      worker.join();
      const qint64 cancel_ms = timer.elapsed();
      Check(cancel_ms <= 100, "WinHTTP 取消 ≤100ms");
      Check(response.cancelled && !response.ok, "WinHTTP 取消返回 cancelled");
      std::printf("T8.1 unit winhttp_cancel_ms=%lld\n",
                  static_cast<long long>(cancel_ms));
    }
  }

  {
    WinHttpClient client;
    HttpCancelToken token;
    HttpRequest request;
    request.url = QStringLiteral("http://127.0.0.1:1/refused");
    request.timeout_ms = 1000;
    const HttpResponse response = client.Get(request, token);
    Check(!response.ok && !response.error.isEmpty(), "连接拒绝给可读错误");
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("[FAIL] WSAStartup\n");
    return 2;
  }

  TestNormalize();
  TestParseView();
  TestResolveClient();
  TestWinHttpClient();

  WSACleanup();
  if (g_failures == 0) {
    std::printf("[OK] T8.1 online resolve tests passed\n");
    return 0;
  }
  std::printf("[FAIL] T8.1 online resolve tests failures=%d\n", g_failures);
  return 1;
}
