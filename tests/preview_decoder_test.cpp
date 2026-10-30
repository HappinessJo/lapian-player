// T2.9 拖动预览解码器：预热、首帧时间、2 秒拖动目标更新下的发布帧率。
// 这是开发机探针，不是 spec 基线机的正式 P3/P4 验收。

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#include <wrl/client.h>
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "media/FrameIndex.h"
#include "media/PreviewDecoder.h"
#ifdef _WIN32
#include "platform/gfx/D3D11Device.h"
#include "platform/gfx/VideoRenderer.h"
#endif

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

void Skip(const std::string& what) {
  std::printf("[SKIP] %s\n", what.c_str());
}

lapian::media::PreviewFrame PollLatest(
    lapian::media::PreviewDecoder* preview, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    lapian::media::PreviewFrame frame = preview->TakeLatestFrame();
    if (frame.frame != nullptr) {
      return frame;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return {};
}

// 正向拖动允许发布中间帧；等待目标帧时丢弃中间帧，验证最终能到目标。
lapian::media::PreviewFrame PollTargetFrame(
    lapian::media::PreviewDecoder* preview, std::int64_t target, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    lapian::media::PreviewFrame frame = preview->TakeLatestFrame();
    if (frame.frame == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (frame.frame_index == target) {
      return frame;
    }
    av_frame_free(&frame.frame);
  }
  return {};
}

// DP-1：把预览线程的硬件帧交给同一个 VideoRenderer，离屏渲染并回读，确认
// 不是黑屏/背景色。等价于 GfxWidget::SetVideoFrame 的同设备纹理上传路径。
#ifdef _WIN32
bool RendersNonBackground(ID3D11Device* device, ID3D11DeviceContext* context,
                          const AVFrame* frame, std::string* error) {
  lapian::gfx::VideoRenderer renderer;
  if (!renderer.Initialize(device, context, error)) {
    return false;
  }
  if (!renderer.SetFrame(frame, 0, error)) {
    return false;
  }
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = 320;
  desc.Height = 180;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, target.GetAddressOf())) ||
      FAILED(device->CreateRenderTargetView(target.Get(), nullptr,
                                            view.GetAddressOf()))) {
    *error = "创建预览渲染目标失败";
    return false;
  }
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) {
    *error = "创建预览回读纹理失败";
    return false;
  }
  renderer.Render(view.Get(), 320, 180);
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    *error = "预览回读失败";
    return false;
  }
  bool found = false;
  const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
  for (UINT y = 0; y < 180 && !found; y += 4) {
    const auto* row = pixels + static_cast<std::size_t>(y) * mapped.RowPitch;
    for (UINT x = 0; x < 320; x += 4) {
      const int b = row[x * 4 + 0];
      const int g = row[x * 4 + 1];
      const int r = row[x * 4 + 2];
      if (std::max({r, g, b}) > 60 || std::min({r, g, b}) < 8) {
        found = true;
        break;
      }
    }
  }
  context->Unmap(staging.Get(), 0);
  if (!found) {
    *error = "预览硬件帧渲染结果为背景色";
  }
  return found;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  std::printf("T2.9 拖动预览解码测试\n");
  const std::string path =
      argc > 1 ? std::string(argv[1]) : "media/proxy/proxy-V1.mp4";
  std::printf("[info] path=%s\n", path.c_str());

  lapian::media::FrameIndex index;
  std::string error;
  if (!index.BuildFromFile(path, -1, &error)) {
    Skip(path + " 不可用: " + error);
    return 77;
  }

  lapian::media::PreviewDecoder preview;
  const auto configure_start = std::chrono::steady_clock::now();
  preview.Configure(path, -1, &index);

  bool ready = false;
  for (int i = 0; i < 200; ++i) {
    if (preview.IsReady()) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const double open_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - configure_start)
          .count();
  std::printf("[perf] preview_open=%.3fms\n", open_ms);
  Check(ready, "预览解码器在 2s 内后台打开");
  if (!ready) {
    std::printf("       error=%s\n", preview.last_error().c_str());
    preview.Close();
    return 1;
  }

  const auto first_start = std::chrono::steady_clock::now();
  preview.Begin(600);
  lapian::media::PreviewFrame first = PollTargetFrame(&preview, 600, 2000);
  const double first_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - first_start)
          .count();
  std::printf("[perf] first_preview=%.3fms\n", first_ms);
  Check(first.frame != nullptr, "拖动开始时能取到首张预览帧");
  // 反向拖动起点现在只发布目标帧：长 GOP 下要等解码到目标，当前画面仍由精确帧保持。
  // 短 GOP 素材通常 <100ms；这里用 1000ms 作为长 GOP 不空等的上限。
  Check(first_ms <= 1000.0, "预热后首张目标预览 ≤1000ms（开发机）");
  if (first.frame != nullptr) {
    const int longest = std::max(first.frame->width, first.frame->height);
    Check(first.frame->width > 0 && first.frame->height > 0 && longest <= 1280,
          "预览帧降采样到最长边 ≤1280");
    Check(first.frame_index == 600, "起点预览==目标帧 600");
    av_frame_free(&first.frame);
  }

  // 模拟 2 秒拖动：每 16ms 更新目标帧，并在 16ms 窗口内取走最新预览帧。
  preview.Begin(0);
  const auto drag_start = std::chrono::steady_clock::now();
  int published = 0;
  std::int64_t target = 0;
  while (std::chrono::steady_clock::now() - drag_start <
         std::chrono::seconds(2)) {
    target = (target + 7) % 720;
    preview.UpdateTarget(target);
    const auto slice_end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    do {
      lapian::media::PreviewFrame frame = preview.TakeLatestFrame();
      if (frame.frame != nullptr) {
        ++published;
        av_frame_free(&frame.frame);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < slice_end);
  }
  const double drag_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - drag_start)
          .count();
  const double fps = static_cast<double>(published) / drag_seconds;
  std::printf("[perf] preview_fps=%.2f (%d frames / %.3fs)\n", fps, published,
              drag_seconds);
  Check(published > 0, "拖动期间有预览帧发布");
  Check(fps >= 30.0, "拖动预览发布 ≥30fps（开发机）");

  // 反向拖动：600→560 每步等目标帧；必须单调不增、每步正好是目标帧、绝不前跳。
  const auto backward_start = std::chrono::steady_clock::now();
  preview.Begin(600);
  lapian::media::PreviewFrame backward_first = PollTargetFrame(&preview, 600, 2000);
  Check(backward_first.frame != nullptr, "反向拖动起点能取到预览帧");
  if (backward_first.frame != nullptr) {
    Check(backward_first.frame_index == 600, "反向拖动起点==600");
    av_frame_free(&backward_first.frame);
  }

  std::int64_t previous_frame = 600;
  int backward_steps = 0;
  bool exact_target = true;
  bool monotonic = true;
  for (std::int64_t step = 599; step >= 560; --step) {
    preview.UpdateTarget(step);
    lapian::media::PreviewFrame frame = PollTargetFrame(&preview, step, 1000);
    if (frame.frame == nullptr) {
      exact_target = false;
      break;
    }
    if (frame.frame_index != step) {
      exact_target = false;
      std::printf("[info] backward target=%lld got=%lld\n",
                  static_cast<long long>(step),
                  static_cast<long long>(frame.frame_index));
    }
    if (frame.frame_index > previous_frame) {
      monotonic = false;
    }
    previous_frame = frame.frame_index;
    ++backward_steps;
    av_frame_free(&frame.frame);
  }
  const double backward_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - backward_start)
          .count();
  std::printf("[perf] backward_steps=%d total=%.3fms avg=%.3fms\n",
              backward_steps, backward_ms,
              backward_steps > 0 ? backward_ms / backward_steps : 0.0);
  Check(backward_steps == 40, "反向拖动 40 步都能取到帧");
  Check(exact_target, "反向拖动每一步发布的都是目标帧");
  Check(monotonic, "反向拖动发布帧单调不增（无向前跳帧）");

  // 反向后小幅向前：应恢复正向发布，且不残留旧目标帧。
  preview.UpdateTarget(561);
  lapian::media::PreviewFrame forward_again =
      PollTargetFrame(&preview, 561, 2000);
  Check(forward_again.frame != nullptr, "反向拖动后向前一步仍能到目标 561");
  if (forward_again.frame != nullptr) {
    av_frame_free(&forward_again.frame);
  }

  // 快速反向拖动：目标每 16ms 回退 1 帧；UI 可能跳过目标，但发布帧绝不能向前跳。
  preview.Begin(600);
  lapian::media::PreviewFrame rapid_start_frame =
      PollTargetFrame(&preview, 600, 2000);
  if (rapid_start_frame.frame != nullptr) {
    av_frame_free(&rapid_start_frame.frame);
  }
  std::int64_t rapid_target = 599;
  std::int64_t rapid_previous = 600;
  int rapid_frames = 0;
  bool rapid_monotonic = true;
  const auto rapid_start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - rapid_start <
         std::chrono::milliseconds(900)) {
    preview.UpdateTarget(rapid_target);
    const auto slice_end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    do {
      lapian::media::PreviewFrame frame = preview.TakeLatestFrame();
      if (frame.frame != nullptr) {
        ++rapid_frames;
        if (frame.frame_index > rapid_previous) {
          rapid_monotonic = false;
        }
        rapid_previous = frame.frame_index;
        av_frame_free(&frame.frame);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < slice_end);
    if (rapid_target > 500) {
      --rapid_target;
    }
  }
  std::printf("[perf] rapid_backward_frames=%d\n", rapid_frames);
  Check(rapid_frames > 0, "快速反向拖动有预览帧发布");
  Check(rapid_monotonic, "快速反向拖动发布帧不向前跳");

  preview.End();
  preview.Close();

  // DP-1：设备为空时自动回退软件，记录 device_unavailable，且仍能出预览帧。
  {
    lapian::media::PreviewDecoder fallback;
    fallback.Configure(path, -1, &index, nullptr, true);
    bool fallback_ready = false;
    for (int i = 0; i < 200; ++i) {
      if (fallback.IsReady()) {
        fallback_ready = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Check(fallback_ready, "无 D3D11 设备时预览解码器仍能打开（软件回退）");
    if (fallback_ready) {
      const lapian::media::PreviewDiagnostics diag = fallback.diagnostics();
      Check(!diag.hw_active, "无设备时 hw_active=0（不假硬解）");
      Check(diag.hw_fallback_reason == "device_unavailable",
            "无设备时记录 device_unavailable");
      fallback.Begin(600);
      lapian::media::PreviewFrame frame = PollTargetFrame(&fallback, 600, 2000);
      Check(frame.frame != nullptr, "软件回退仍能发布预览帧");
      if (frame.frame != nullptr) {
        Check(std::max(frame.frame->width, frame.frame->height) <= 1280,
              "软件回退帧仍降采样到最长边 ≤1280");
        av_frame_free(&frame.frame);
      }
      fallback.End();
    }
    fallback.Close();
  }

#ifdef _WIN32
  // DP-1：与渲染器共享 D3D11 设备硬解；无硬解/D3D11 的开发机跳过。
  {
    lapian::gfx::D3D11Device device;
    std::string device_error;
    if (!device.Create(&device_error)) {
      Skip("D3D11 设备创建失败: " + device_error);
    } else {
      lapian::media::PreviewDecoder hardware;
      hardware.Configure(path, -1, &index, device.device(), true);
      bool ready = false;
      for (int i = 0; i < 300; ++i) {
        if (hardware.IsReady()) {
          ready = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      Check(ready, "共享设备预览解码器能打开");
      lapian::media::PreviewDiagnostics diag = hardware.diagnostics();
      if (!ready || !diag.hw_active) {
        Skip("共享设备预览硬解不可用: " + diag.hw_fallback_reason);
      } else {
        Check(diag.hw_fallback_reason.empty(), "硬解 active 时无回退原因");
        hardware.Begin(600);
        lapian::media::PreviewFrame frame = PollTargetFrame(&hardware, 600, 3000);
        Check(frame.frame != nullptr, "共享设备硬解能发布目标帧");
        if (frame.frame != nullptr) {
          Check(frame.frame->hw_frames_ctx != nullptr ||
                    frame.frame->format == AV_PIX_FMT_D3D11,
                "预览帧是 D3D11 硬件帧");
          Check(frame.frame_index == 600, "硬解预览帧号==目标 600");
          Check(frame.frame->width >= 1280,
                "硬解预览帧保持全分辨率（未走 CPU 降采样）");
          std::string render_error;
          Check(RendersNonBackground(device.device(), device.context(),
                                     frame.frame, &render_error),
                "共享设备硬件帧能渲染出非背景像素");
          if (!render_error.empty()) {
            std::printf("       render_error=%s\n", render_error.c_str());
          }
          av_frame_free(&frame.frame);
        }
        diag = hardware.diagnostics();
        Check(diag.seeks > 0, "硬解预览记录了 seek 次数");
        Check(diag.published > 0, "硬解预览记录了发布帧数");
        hardware.End();
      }
      hardware.Close();

      // 设备存在但明确要求软件：记录 software_requested，帧降采样。
      lapian::media::PreviewDecoder software;
      software.Configure(path, -1, &index, device.device(), false);
      bool software_ready = false;
      for (int i = 0; i < 200; ++i) {
        if (software.IsReady()) {
          software_ready = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      Check(software_ready, "有设备但要求软件时预览解码器能打开");
      if (software_ready) {
        const lapian::media::PreviewDiagnostics software_diag =
            software.diagnostics();
        Check(!software_diag.hw_active, "要求软件时 hw_active=0");
        Check(software_diag.hw_fallback_reason == "software_requested",
              "要求软件时记录 software_requested");
        software.Begin(600);
        lapian::media::PreviewFrame frame =
            PollTargetFrame(&software, 600, 2000);
        Check(frame.frame != nullptr, "要求软件时仍能发布预览帧");
        if (frame.frame != nullptr) {
          Check(std::max(frame.frame->width, frame.frame->height) <= 1280,
                "要求软件时预览帧降采样");
          av_frame_free(&frame.frame);
        }
        software.End();
      }
      software.Close();
    }
  }
#endif

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
