// T3.1 音频单测：音量映射、环形缓冲、格式转换，以及 V9 的真实 AAC 解码/seek。
// 媒体素材缺失时返回 77（CTest SKIP_RETURN_CODE）。

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "audio/AudioDecoder.h"
#include "audio/AudioFormat.h"

namespace {

int g_failures = 0;
bool g_skipped = false;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckNear(double actual, double expected, double epsilon, const std::string& what) {
  const bool ok = std::fabs(actual - expected) <= epsilon;
  if (ok) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s (actual=%.9f expected=%.9f)\n", what.c_str(), actual, expected);
    ++g_failures;
  }
}

void Skip(const std::string& what) {
  std::printf("[SKIP] %s\n", what.c_str());
  g_skipped = true;
}

std::string PathToUtf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

void TestVolumeMapping() {
  using lapian::audio::VolumeToGain;
  CheckNear(VolumeToGain(-10), 0.0, 0.0, "音量 -10 clamp 到 0");
  CheckNear(VolumeToGain(0), 0.0, 0.0, "音量 0 增益 0");
  CheckNear(VolumeToGain(25), 0.0625, 1e-6, "音量 25 平方增益");
  CheckNear(VolumeToGain(50), 0.25, 1e-6, "音量 50 平方增益");
  CheckNear(VolumeToGain(100), 1.0, 1e-6, "音量 100 增益 1");
  CheckNear(VolumeToGain(150), 1.0, 1e-6, "音量 150 clamp 到 1");
}

void TestRingBuffer() {
  using lapian::audio::AudioRingBuffer;

  AudioRingBuffer ring;
  Check(!ring.valid(), "未 Reset 的环形缓冲无效");

  ring.Reset(8, 2);
  Check(ring.valid(), "Reset 后环形缓冲有效");
  Check(ring.capacity_frames() == 8, "环形缓冲容量 8 帧");
  Check(ring.free_frames() == 8, "初始空闲 8 帧");

  const float first[] = {1, 2, 3, 4, 5, 6};
  Check(ring.Push(first, 3) == 3, "写入 3 帧");
  Check(ring.size_frames() == 3 && ring.free_frames() == 5, "写入后计数正确");

  float out[16] = {};
  Check(ring.Pop(out, 2) == 2, "读出 2 帧");
  CheckNear(out[0], 1.0, 0.0, "读出帧 0 左声道");
  CheckNear(out[1], 2.0, 0.0, "读出帧 0 右声道");
  CheckNear(out[2], 3.0, 0.0, "读出帧 1 左声道");
  CheckNear(out[3], 4.0, 0.0, "读出帧 1 右声道");
  Check(ring.size_frames() == 1, "读出后剩 1 帧");

  const float second[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
  Check(ring.Push(second, 6) == 6, "跨环形边界写入 6 帧");
  Check(ring.size_frames() == 7, "环形边界后共 7 帧");
  Check(ring.Push(second, 2) == 1, "容量不足时只接受 1 帧");
  Check(ring.size_frames() == 8, "环形缓冲写满");

  float full[32] = {};
  Check(ring.Pop(full, 8) == 8, "读出全部 8 帧");
  Check(ring.size_frames() == 0 && ring.free_frames() == 8, "读空后计数归位");
  CheckNear(full[0], 5.0, 0.0, "环形缓冲首帧保持顺序");
  CheckNear(full[14], 10.0, 0.0, "环形缓冲跨边界顺序正确");

  ring.Clear();
  Check(ring.size_frames() == 0, "Clear 清空");
}

void TestSampleConversion() {
  using lapian::audio::ConvertFloatToS16;
  using lapian::audio::ConvertS16ToFloat;

  const float input[] = {-2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f};
  std::int16_t pcm[7] = {};
  ConvertFloatToS16(input, pcm, 7);
  Check(pcm[0] == -32767, "float -2 钳到 -32767");
  Check(pcm[1] == -32767, "float -1 转 -32767");
  Check(pcm[3] == 0, "float 0 转 0");
  Check(pcm[5] == 32767, "float 1 转 32767");
  Check(pcm[6] == 32767, "float 2 钳到 32767");

  float roundtrip[7] = {};
  ConvertS16ToFloat(pcm, roundtrip, 7);
  CheckNear(roundtrip[3], 0.0, 1e-9, "PCM16 0 回转 float");
  CheckNear(roundtrip[5], 32767.0 / 32768.0, 1e-6, "PCM16 满幅回转 float");
}

bool DecodeV9(lapian::audio::AudioDecoder* decoder) {
  if (!std::filesystem::exists("media/V9.mp4")) {
    Skip("media/V9.mp4 不存在");
    return false;
  }

  lapian::audio::AudioFormat format;
  format.sample_rate = 48000;
  format.channels = 2;
  std::string error;
  if (!decoder->Open("media/V9.mp4", 1, format, &error)) {
    Check(false, "打开 V9 音频失败: " + error);
    return false;
  }
  Check(true, "打开 V9 音轨");
  Check(decoder->info().stream_index == 1, "V9 音轨索引为 1");
  Check(decoder->info().codec.find("aac") != std::string::npos, "V9 音频编码为 AAC");
  Check(decoder->info().source_sample_rate == 48000, "V9 源采样率 48kHz");
  Check(decoder->info().source_channels == 1, "V9 源单声道");

  std::size_t total_frames = 0;
  double first_pts = 0.0;
  bool have_first = false;
  bool finite = true;
  bool stereo_equal = true;
  double click_peak_time = 0.0;
  int click_peak = 0;
  const double click_start = 100.0 / 24.0 - 1.0 / 24.0;
  const double click_end = 100.0 / 24.0 + 1.0 / 24.0;
  for (int guard = 0; guard < 10000; ++guard) {
    lapian::audio::AudioChunk chunk;
    const lapian::audio::AudioDecodeResult result =
        decoder->DecodeChunk(1024, &chunk, &error);
    if (result == lapian::audio::AudioDecodeResult::kEnd) {
      break;
    }
    if (result == lapian::audio::AudioDecodeResult::kError) {
      Check(false, "解码 V9 音频失败: " + error);
      return false;
    }
    if (chunk.frames == 0) {
      continue;
    }
    if (!have_first) {
      first_pts = chunk.pts_seconds;
      have_first = true;
    }
    total_frames += chunk.frames;
    for (std::size_t i = 0; i < chunk.samples.size(); ++i) {
      if (!std::isfinite(chunk.samples[i])) {
        finite = false;
      }
    }
    for (std::size_t frame = 0; frame + 1 < chunk.samples.size(); frame += 2) {
      if (std::fabs(chunk.samples[frame] - chunk.samples[frame + 1]) > 1e-6f) {
        stereo_equal = false;
      }
      const double sample_time =
          chunk.pts_seconds + static_cast<double>(frame / 2) / 48000.0;
      if (sample_time >= click_start && sample_time <= click_end) {
        const int magnitude = static_cast<int>(
            std::fabs(chunk.samples[frame]) * 32768.0f);
        if (magnitude > click_peak) {
          click_peak = magnitude;
          click_peak_time = sample_time;
        }
      }
    }
  }

  Check(finite, "V9 解码样本均为有限值");
  Check(stereo_equal, "单声道上混到双声道左右一致");
  CheckNear(static_cast<double>(total_frames) / 48000.0, 5.0, 0.15, "V9 音频约 5 秒");
  Check(have_first, "V9 解码出第一块音频");
  if (have_first) {
    Check(first_pts > -0.1 && first_pts < 0.1, "V9 首块 PTS 接近 0");
  }
  Check(click_peak > 500, "V9 第 100 帧附近有 click 峰值");
  CheckNear(click_peak_time, 100.0 / 24.0, 1.0 / 24.0,
            "V9 click 峰值在视频第 100 帧 ±1 帧内");

  if (!decoder->SeekTo(2.0, &error)) {
    Check(false, "V9 音频 seek 2.0s 失败: " + error);
    return false;
  }
  lapian::audio::AudioChunk seek_chunk;
  const lapian::audio::AudioDecodeResult seek_result =
      decoder->DecodeChunk(1024, &seek_chunk, &error);
  Check(seek_result == lapian::audio::AudioDecodeResult::kFrame, "seek 后解出音频块");
  if (seek_result == lapian::audio::AudioDecodeResult::kFrame) {
    CheckNear(seek_chunk.pts_seconds, 2.0, 0.05, "seek 后首块 PTS 接近 2.0s");
    Check(seek_chunk.frames > 0, "seek 后音频块非空");
  }
  return true;
}

void TestOpenFailure() {
  lapian::audio::AudioDecoder decoder;
  lapian::audio::AudioFormat format;
  format.sample_rate = 48000;
  format.channels = 2;
  std::string error;
  Check(!decoder.Open("media/does-not-exist.mp4", -1, format, &error), "不存在的音频文件打开失败");
  Check(!error.empty(), "打开失败有可读错误");
}

}  // namespace

int main() {
  std::printf("T3.1 音频测试\n");

  TestVolumeMapping();
  TestRingBuffer();
  TestSampleConversion();
  TestOpenFailure();

  lapian::audio::AudioDecoder decoder;
  DecodeV9(&decoder);

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  if (g_skipped) {
    std::printf("部分媒体素材缺失，测试跳过\n");
    return 77;
  }
  std::printf("全部通过\n");
  return 0;
}
