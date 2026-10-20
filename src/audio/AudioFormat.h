#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lapian::audio {

// WASAPI shared mode 的混合格式只支持少数几种；解码器统一输出 float32，
// 由渲染层按设备格式做最后一步转换。
enum class SampleEncoding {
  kFloat32,
  kInt16,
};

struct AudioFormat {
  int sample_rate = 0;
  int channels = 0;
  SampleEncoding encoding = SampleEncoding::kFloat32;

  bool valid() const { return sample_rate > 0 && channels > 0; }
  int bytes_per_sample() const {
    return encoding == SampleEncoding::kFloat32 ? 4 : 2;
  }
  int bytes_per_frame() const { return bytes_per_sample() * channels; }
};

// 0–100 映射到软件增益。平方曲线让滑块中段更接近听感上的“一半”，
// 与 Windows 常见音量滑块的感知一致；静音由调用方单独处理。
float VolumeToGain(int percent);

// 单生产者/单消费者的整帧环形缓冲；元素为交错 float32。
// 只在音频工作线程内读写，不提供跨线程同步。
class AudioRingBuffer {
 public:
  AudioRingBuffer() = default;

  void Reset(std::size_t capacity_frames, int channels);
  void Clear();

  bool valid() const { return capacity_frames_ > 0 && channels_ > 0; }
  std::size_t capacity_frames() const { return capacity_frames_; }
  std::size_t size_frames() const { return size_; }
  std::size_t free_frames() const { return capacity_frames_ - size_; }
  int channels() const { return channels_; }

  // 返回实际写入的帧数（容量不足时截断）。
  std::size_t Push(const float* samples, std::size_t frames);
  // 返回实际读出的帧数（数据不足时截断）。
  std::size_t Pop(float* out, std::size_t frames);

 private:
  std::vector<float> data_;
  std::size_t capacity_frames_ = 0;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
  int channels_ = 0;
};

void ConvertFloatToS16(const float* input, std::int16_t* output, std::size_t samples);
void ConvertS16ToFloat(const std::int16_t* input, float* output, std::size_t samples);

}  // namespace lapian::audio
