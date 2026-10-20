#include "app/SnapshotService.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QString>

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "app/SnapshotImage.h"
#include "media/SnapshotNaming.h"
#include "platform/gfx/D3D11Context.h"

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMaxQueue = 4;

std::string AvErrorText(int error) {
  if (error >= 0) {
    return {};
  }
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  if (av_strerror(error, buffer, sizeof(buffer)) < 0) {
    return {};
  }
  return buffer;
}

QString HResultText(HRESULT hr) {
  return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

bool ConvertToBgra(const AVFrame* source, QImage* image, QString* error) {
  if (source == nullptr || image == nullptr) {
    *error = QStringLiteral("源帧为空");
    return false;
  }
  const int width = source->width;
  const int height = source->height;
  if (width <= 0 || height <= 0) {
    *error = QStringLiteral("源帧尺寸无效");
    return false;
  }

  QImage result(width, height, QImage::Format_RGB32);
  if (result.isNull()) {
    *error = QStringLiteral("分配截图缓冲失败（%1x%2）").arg(width).arg(height);
    return false;
  }

  SwsContext* sws = sws_getCachedContext(
      nullptr, width, height, static_cast<AVPixelFormat>(source->format), width, height,
      AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (sws == nullptr) {
    *error = QStringLiteral("创建截图色彩转换上下文失败");
    return false;
  }

  uint8_t* destination[4] = {result.bits(), nullptr, nullptr, nullptr};
  int destination_linesize[4] = {static_cast<int>(result.bytesPerLine()), 0, 0, 0};
  const int scaled = sws_scale(sws, source->data, source->linesize, 0, height, destination,
                               destination_linesize);
  sws_freeContext(sws);
  if (scaled <= 0) {
    *error = QStringLiteral("截图 YUV→BGRA 转换失败");
    return false;
  }

  // QImage::Format_RGB32 的 alpha 语义固定为 255；sws 一般已写 255，这里兜底，
  // 避免 PNG 出现透明像素。
  const int stride = static_cast<int>(result.bytesPerLine());
  for (int y = 0; y < height; ++y) {
    uint8_t* row = result.bits() + static_cast<std::ptrdiff_t>(y) * stride;
    for (int x = 0; x < width; ++x) {
      row[x * 4 + 3] = 255;
    }
  }

  *image = std::move(result);
  return true;
}

bool EncodePng(const QImage& image, QByteArray* output, QString* error) {
  if (image.isNull()) {
    *error = QStringLiteral("编码前图像为空");
    return false;
  }
  if (image.format() != QImage::Format_RGB32) {
    *error = QStringLiteral("编码图像格式不是 RGB32");
    return false;
  }

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory,
                                reinterpret_cast<void**>(factory.GetAddressOf()));
  if (FAILED(hr)) {
    *error = QStringLiteral("创建 WIC 工厂失败（%1）").arg(HResultText(hr));
    return false;
  }

  ComPtr<IStream> stream;
  hr = CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf());
  if (FAILED(hr)) {
    *error = QStringLiteral("创建 PNG 内存流失败（%1）").arg(HResultText(hr));
    return false;
  }

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
  if (FAILED(hr)) {
    *error = QStringLiteral("创建 PNG 编码器失败（%1）").arg(HResultText(hr));
    return false;
  }
  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    *error = QStringLiteral("初始化 PNG 编码器失败（%1）").arg(HResultText(hr));
    return false;
  }

  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  hr = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
  if (FAILED(hr)) {
    *error = QStringLiteral("创建 PNG 帧失败（%1）").arg(HResultText(hr));
    return false;
  }
  hr = frame->Initialize(properties.Get());
  if (FAILED(hr)) {
    *error = QStringLiteral("初始化 PNG 帧失败（%1）").arg(HResultText(hr));
    return false;
  }

  const UINT width = static_cast<UINT>(image.width());
  const UINT height = static_cast<UINT>(image.height());
  hr = frame->SetSize(width, height);
  if (FAILED(hr)) {
    *error = QStringLiteral("设置 PNG 尺寸失败（%1）").arg(HResultText(hr));
    return false;
  }

  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  hr = frame->SetPixelFormat(&format);
  if (FAILED(hr) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
    *error = QStringLiteral("PNG 编码器不接受 BGRA 像素格式");
    return false;
  }

  const UINT stride = static_cast<UINT>(image.bytesPerLine());
  const ULONGLONG byte_count =
      static_cast<ULONGLONG>(stride) * static_cast<ULONGLONG>(height);
  if (byte_count == 0 || byte_count > std::numeric_limits<UINT>::max()) {
    *error = QStringLiteral("PNG 图像过大");
    return false;
  }
  hr = frame->WritePixels(height, stride, static_cast<UINT>(byte_count),
                          const_cast<BYTE*>(image.constBits()));
  if (FAILED(hr)) {
    *error = QStringLiteral("写入 PNG 像素失败（%1）").arg(HResultText(hr));
    return false;
  }
  hr = frame->Commit();
  if (FAILED(hr)) {
    *error = QStringLiteral("提交 PNG 帧失败（%1）").arg(HResultText(hr));
    return false;
  }
  hr = encoder->Commit();
  if (FAILED(hr)) {
    *error = QStringLiteral("提交 PNG 编码器失败（%1）").arg(HResultText(hr));
    return false;
  }

  STATSTG stat{};
  hr = stream->Stat(&stat, STATFLAG_NONAME);
  if (FAILED(hr)) {
    *error = QStringLiteral("读取 PNG 内存流大小失败（%1）").arg(HResultText(hr));
    return false;
  }
  const ULONGLONG size = stat.cbSize.QuadPart;
  if (size == 0 || size > 512ull * 1024 * 1024) {
    *error = QStringLiteral("PNG 内存流大小异常");
    return false;
  }

  LARGE_INTEGER zero{};
  hr = stream->Seek(zero, STREAM_SEEK_SET, nullptr);
  if (FAILED(hr)) {
    *error = QStringLiteral("定位 PNG 内存流失败（%1）").arg(HResultText(hr));
    return false;
  }

  output->resize(static_cast<int>(size));
  ULONGLONG total = 0;
  while (total < size) {
    ULONG read = 0;
    const ULONG request = static_cast<ULONG>(
        std::min<ULONGLONG>(size - total, std::numeric_limits<ULONG>::max()));
    hr = stream->Read(output->data() + total, request, &read);
    if (FAILED(hr) || read == 0) {
      *error = QStringLiteral("读取 PNG 内存流失败（%1）").arg(HResultText(hr));
      return false;
    }
    total += read;
  }
  output->resize(static_cast<int>(total));
  return true;
}

}  // namespace

SnapshotService::SnapshotService(QString pictures_root, QObject* parent)
    : QObject(parent), pictures_root_(std::move(pictures_root)) {
  thread_ = std::thread([this]() { WorkerLoop(); });
}

SnapshotService::~SnapshotService() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }

  // 正常路径由 Drain()/WorkerLoop 消费队列；这里兜底释放异常退出遗留的引用。
  for (Task& task : queue_) {
    av_frame_free(&task.frame);
  }
  queue_.clear();
}

bool SnapshotService::Capture(const AVFrame* frame, const Request& request) {
  if (frame == nullptr) {
    return false;
  }
  AVFrame* reference = av_frame_alloc();
  if (reference == nullptr) {
    return false;
  }
  if (av_frame_ref(reference, frame) < 0) {
    av_frame_free(&reference);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || queue_.size() >= kMaxQueue) {
      av_frame_free(&reference);
      return false;
    }
    queue_.push_back(Task{reference, request});
  }
  cv_.notify_one();
  return true;
}

void SnapshotService::SetPicturesRoot(const QString& root) {
  std::lock_guard<std::mutex> lock(mutex_);
  pictures_root_ = root;
}

void SnapshotService::Drain() {
  if (!thread_.joinable()) {
    return;
  }
  std::unique_lock<std::mutex> lock(mutex_);
  idle_cv_.wait(lock, [this]() { return queue_.empty() && !busy_; });
}

int SnapshotService::PendingCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(queue_.size()) + (busy_ ? 1 : 0);
}

void SnapshotService::WorkerLoop() {
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool com_ready =
      SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
  const bool com_needs_uninit = SUCCEEDED(com_result);

  while (true) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) {
        if (stopping_) {
          break;
        }
        continue;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
      busy_ = true;
    }

    QString path;
    QString error;
    bool fallback = false;
    qint64 encode_ms = 0;
    bool saved = false;
    if (!com_ready) {
      error = QStringLiteral("截图线程 COM 初始化失败（%1）").arg(HResultText(com_result));
    } else {
      try {
        saved = ProcessTask(task, &path, &fallback, &error, &encode_ms);
      } catch (const std::exception& exception) {
        error = QStringLiteral("截图线程异常：%1")
                    .arg(QString::fromUtf8(exception.what()));
      } catch (...) {
        error = QStringLiteral("截图线程未知异常");
      }
    }

    // 先发信号再清 busy_：Drain() 返回时 snapshotSaved/snapshotFailed 已经
    // 投递到接收线程的事件队列，调用方再 processEvents 就能确定拿到结果。
    // 若反过来先置 idle，Drain() 可能抢在信号入队前返回（4K 编码更长时可见）。
    if (saved) {
      emit snapshotSaved(path, fallback, static_cast<qint64>(task.request.frame), encode_ms);
    } else {
      emit snapshotFailed(error);
    }

    av_frame_free(&task.frame);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      busy_ = false;
    }
    idle_cv_.notify_all();
  }

  if (com_needs_uninit) {
    CoUninitialize();
  }
}

bool SnapshotService::ProcessTask(const Task& task, QString* path, bool* used_fallback,
                                  QString* error, qint64* encode_ms) {
  const auto started = std::chrono::steady_clock::now();
  AVFrame* cpu_frame = nullptr;
  const AVFrame* source = task.frame;
  const bool hardware =
      task.frame->hw_frames_ctx != nullptr || task.frame->format == AV_PIX_FMT_D3D11;
  if (hardware) {
    cpu_frame = av_frame_alloc();
    if (cpu_frame == nullptr) {
      *error = QStringLiteral("分配硬解回读帧失败");
      return false;
    }
    int transfer = 0;
    {
      // 硬解回读与解码器共用 D3D11 立即上下文；与 VideoDecoder 使用同一把锁。
      std::lock_guard<std::recursive_mutex> lock(lapian::gfx::D3D11ContextMutex());
      transfer = av_hwframe_transfer_data(cpu_frame, task.frame, 0);
    }
    if (transfer < 0) {
      *error = QStringLiteral("硬解帧回读失败：%1")
                   .arg(QString::fromStdString(AvErrorText(transfer)));
      av_frame_free(&cpu_frame);
      return false;
    }
    source = cpu_frame;
  }

  QImage image;
  const bool converted = ConvertToBgra(source, &image, error);
  av_frame_free(&cpu_frame);
  if (!converted) {
    return false;
  }

  QImage display = lapian::snapshot::RotateSnapshotImage(
      image, task.request.rotation_counterclockwise_degrees);
  if (display.format() != QImage::Format_RGB32) {
    display = display.convertToFormat(QImage::Format_RGB32);
  }
  if (display.isNull()) {
    *error = QStringLiteral("截图旋转失败");
    return false;
  }

  QByteArray png;
  if (!EncodePng(display, &png, error)) {
    return false;
  }

  QString directory_error;
  QString pictures_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pictures_root = pictures_root_;
  }
  const lapian::snapshot::SnapshotDirectory directory =
      lapian::snapshot::ResolveSnapshotDirectory(task.request.video_path, pictures_root,
                                                 &directory_error);
  if (directory.path.isEmpty()) {
    *error = directory_error.isEmpty() ? QStringLiteral("截图目录不可写") : directory_error;
    return false;
  }

  const QString base_name = lapian::snapshot::BuildSnapshotBaseName(
      task.request.video_path, task.request.frame, task.request.fps_num,
      task.request.fps_den);

  for (int sequence = 0; sequence < 1000; ++sequence) {
    const QString candidate =
        lapian::snapshot::SnapshotFilePath(directory.path, base_name, sequence);
    QFile file(candidate);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
      if (QFileInfo::exists(candidate)) {
        continue;  // 重名：试下一个序号，绝不覆盖。
      }
      *error = QStringLiteral("无法创建截图文件：%1（%2）")
                   .arg(QDir::toNativeSeparators(candidate), file.errorString());
      return false;
    }

    const qint64 written = file.write(png);
    const bool flushed = written == static_cast<qint64>(png.size()) && file.flush();
    file.close();
    if (!flushed || file.error() != QFileDevice::NoError) {
      file.remove();
      *error = QStringLiteral("写入截图文件失败：%1")
                   .arg(QDir::toNativeSeparators(candidate));
      return false;
    }

    *path = candidate;
    *used_fallback = directory.fallback;
    *encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - started)
                     .count();
    return true;
  }

  *error = QStringLiteral("截图重名次数过多，无法生成文件名");
  return false;
}
