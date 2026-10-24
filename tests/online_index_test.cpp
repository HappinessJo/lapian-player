// T8.11 渐进索引/波形离线单测（无 GUI、无网络）。
//
// 用 T8.10 的真实 OB-3 缓存（1080P25/212.261s，视频 69.7MB + 独立 AAC 5.4MB）
// 模拟 1–2MB/s 的受控慢增长：每 256KB Publish 后推进一次增量索引，验证
// - 只扫已发布前缀、stable 单调、已暴露前缀 PTS 不变；
// - 已缓存 30s 时索引已覆盖（留 tail guard/2s 容差）；
// - 完成时 exact 总帧数与完整一次性索引一致；
// - 累计读字节 / 文件大小接近 1（不随周期全量重扫）。
// 波形用同一份独立音频慢增长：bucket 只追加、早期前缀不变、完成后覆盖时长、
// 偏移 ≤1 帧。
//
// 素材缺失时返回 77（CTest SKIP）。

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/AudioDecoder.h"
#include "audio/WaveformGenerator.h"
#include "media/MediaProbe.h"
#include "media/OnlineIndexBuilder.h"
#include "media/VideoDecoder.h"
#include "online/GrowingSource.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* name, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
  }
  std::fprintf(stderr, "online_index_check %s ok=%d%s%s\n", name, ok ? 1 : 0,
               detail.empty() ? "" : " detail=", detail.c_str());
  std::fflush(stderr);
}

QString FindVideo() {
  const QString root = QDir::currentPath();
  const QStringList candidates = {
      root + QStringLiteral(
                 "/build/t8.10-final-ob3/cache/BV1GJ411x7h7/137649199/80-7/"
                 "data.mp4"),
      root + QStringLiteral("/build/test-media/faststart-60s.mp4"),
      root + QStringLiteral("/build/test-media/faststart-720p.mp4"),
  };
  for (const QString& candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isFile() && info.size() > 0) {
      return info.absoluteFilePath();
    }
  }
  return QString();
}

QString FindAudio(const QString& video_path) {
  const QString root = QDir::currentPath();
  const QStringList candidates = {
      root + QStringLiteral(
                 "/build/t8.10-final-ob3/cache/BV1GJ411x7h7/137649199/"
                 "audio-30280/data.mp4"),
      video_path,
      root + QStringLiteral("/media/V9.mp4"),
  };
  for (const QString& candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isFile() && info.size() > 0) {
      return info.absoluteFilePath();
    }
  }
  return QString();
}

double DurationSeconds(const lapian::media::MediaProbeResult& probe) {
  return static_cast<double>(probe.info.duration_us) / 1.0e6;
}

}  // namespace

int main() {
  const QString video_path = FindVideo();
  if (video_path.isEmpty()) {
    std::fprintf(stderr, "online_index_skip reason=missing_media\n");
    return 77;
  }
  const qint64 video_total = QFileInfo(video_path).size();
  const lapian::media::MediaProbeResult video_probe =
      lapian::media::ProbeMedia(video_path.toUtf8().constData());
  Check(video_probe.ok, "video_probe", video_probe.error_detail);
  const double fps = video_probe.info.video.frame_rate.ToDouble();
  const double duration_s = DurationSeconds(video_probe);
  Check(fps > 0.0, "video_fps");
  Check(duration_s > 0.0, "video_duration");
  if (!video_probe.ok || fps <= 0.0 || duration_s <= 0.0) {
    return g_failures == 0 ? 1 : g_failures;
  }
  std::fprintf(stderr,
               "online_index_media path=%s bytes=%lld fps=%.3f duration=%.3f\n",
               video_path.toUtf8().constData(),
               static_cast<long long>(video_total), fps, duration_s);
  std::fflush(stderr);

  // 完整文件的一次性索引基准（精确总帧数）。
  {
    auto full_source =
        std::make_shared<lapian::online::GrowingSource>(video_total);
    full_source->Publish(video_total);
    const lapian::media::OnlineIndexResult full =
        lapian::media::BuildOnlineIndex(
            full_source, video_path.toUtf8().constData(),
            video_probe.info.video.stream_index, 0, nullptr);
    Check(full.ok, "full_index_ok", full.error);
    Check(full.complete, "full_index_complete");
    if (!full.ok || !full.complete) {
      std::fprintf(stderr, "online_index_result checks=%d failures=%d\n",
                   g_checks, g_failures);
      return 1;
    }
    const int expected = static_cast<int>(full.index.size());
    std::fprintf(stderr, "online_index_full frames=%d\n", expected);
    std::fflush(stderr);

    // 受控增长：每步 256KB；不 sleep，等价于 1–2MB/s 下每次 Step 新增的量。
    auto source = std::make_shared<lapian::online::GrowingSource>(video_total);
    const qint64 chunk = 256 * 1024;
    qint64 published = std::min<qint64>(2 * chunk, video_total);
    source->Publish(published);
    std::string create_error;
    auto builder = lapian::media::IncrementalOnlineIndexBuilder::Create(
        source, video_path.toUtf8().constData(),
        video_probe.info.video.stream_index, 48, &create_error);
    Check(builder != nullptr, "incremental_create", create_error);
    if (builder == nullptr) {
      return 1;
    }

    const int expected_30 = static_cast<int>(std::llround(30.0 * fps));
    const int tolerance = 48 + static_cast<int>(std::ceil(fps * 2.0));
    QElapsedTimer wall;
    wall.start();
    int stable_at_30 = -1;
    qint64 stable30_ms = -1;
    qint64 published_at_30 = -1;
    qint64 read_bytes_at_30 = -1;
    bool saw_incomplete = false;
    bool tail_guard_ok = true;
    bool stable_monotonic = true;
    bool prefix_stable = true;
    int previous_stable = 0;
    int last_total = -1;
    std::vector<std::int64_t> previous_prefix;
    int snapshots = 0;
    lapian::media::OnlineIndexResult snapshot;
    while (!builder->complete()) {
      const lapian::media::OnlineIndexStepInfo step =
          builder->Step(nullptr, 10000, 1000000);
      if (!step.ok) {
        Check(false, "incremental_step_ok", step.error);
        break;
      }
      if (builder->total_frames() != last_total) {
        lapian::media::OnlineIndexResult fresh;
        if (builder->Snapshot(&fresh)) {
          ++snapshots;
          last_total = builder->total_frames();
          if (!fresh.complete) {
            saw_incomplete = true;
            const int expected_stable =
                std::max(0, builder->total_frames() - 48);
            if (fresh.stable_frames != expected_stable) {
              tail_guard_ok = false;
            }
          }
          if (fresh.stable_frames < previous_stable) {
            stable_monotonic = false;
          }
          const int compare =
              std::min(previous_stable, fresh.stable_frames);
          for (int i = 0; i < compare; ++i) {
            if (fresh.index.PtsOfFrame(i) !=
                previous_prefix[static_cast<std::size_t>(i)]) {
              prefix_stable = false;
              break;
            }
          }
          previous_prefix.resize(
              static_cast<std::size_t>(fresh.stable_frames));
          for (int i = 0; i < fresh.stable_frames; ++i) {
            previous_prefix[static_cast<std::size_t>(i)] =
                fresh.index.PtsOfFrame(i);
          }
          previous_stable = fresh.stable_frames;
          snapshot = std::move(fresh);
          const double cached_s =
              duration_s * static_cast<double>(source->Published()) /
              static_cast<double>(video_total);
          (void)cached_s;
          if (stable30_ms < 0 && snapshot.stable_frames >= expected_30) {
            stable30_ms = wall.elapsed();
            stable_at_30 = snapshot.stable_frames;
            published_at_30 =
                static_cast<qint64>(source->Published());
            read_bytes_at_30 = builder->stats().read_bytes;
            std::fprintf(
                stderr,
                "online_index_stable30 published=%lld total_bytes=%lld "
                "total_frames=%d stable=%d read_bytes=%lld lag_bytes=%lld "
                "ms=%lld\n",
                static_cast<long long>(published_at_30),
                static_cast<long long>(video_total), builder->total_frames(),
                snapshot.stable_frames,
                static_cast<long long>(read_bytes_at_30),
                static_cast<long long>(published_at_30 - read_bytes_at_30),
                static_cast<long long>(stable30_ms));
            std::fflush(stderr);
          }
        }
      }
      if (step.complete) {
        break;
      }
      if (published < video_total) {
        published = std::min<qint64>(video_total, published + chunk);
        source->Publish(published);
      } else if (step.at_frontier) {
        // 已全部发布但还没读到 EOF：再推一次 Step。
        continue;
      }
    }
    if (!builder->Snapshot(&snapshot)) {
      Check(false, "incremental_final_snapshot", builder->last_error());
    }
    const lapian::media::OnlineIndexStats stats = builder->stats();
    const double read_ratio =
        static_cast<double>(stats.read_bytes) /
        static_cast<double>(video_total);
    std::fprintf(
        stderr,
        "online_index_incremental stable=%d expected=%d snapshots=%d "
        "stable30_ms=%lld published_at_30=%lld read_at_30=%lld "
        "tail_guard_ok=%d monotonic=%d prefix_stable=%d steps=%lld opens=%lld "
        "reopens=%lld skips=%lld read_bytes=%lld ratio=%.3f max_step_ms=%.2f "
        "complete=%d\n",
        snapshot.stable_frames, expected, snapshots,
        static_cast<long long>(stable30_ms),
        static_cast<long long>(published_at_30),
        static_cast<long long>(read_bytes_at_30), tail_guard_ok ? 1 : 0,
        stable_monotonic ? 1 : 0, prefix_stable ? 1 : 0,
        static_cast<long long>(stats.steps),
        static_cast<long long>(stats.opens),
        static_cast<long long>(stats.reopens),
        static_cast<long long>(stats.resume_skips),
        static_cast<long long>(stats.read_bytes), read_ratio,
        stats.max_step_ms, snapshot.complete ? 1 : 0);
    std::fflush(stderr);

    Check(saw_incomplete, "saw_incomplete_snapshots");
    Check(tail_guard_ok, "tail_guard_exact");
    Check(stable_monotonic, "stable_monotonic");
    Check(prefix_stable, "prefix_pts_stable");
    Check(stable_at_30 >= expected_30 - tolerance, "index_covers_30s",
          "stable_at_30=" + std::to_string(stable_at_30) +
              " expected30=" + std::to_string(expected_30));
    Check(stable30_ms >= 0 && published_at_30 > 0 &&
              published_at_30 < video_total,
          "index_30s_before_complete",
          "published=" + std::to_string(published_at_30) +
              " total=" + std::to_string(video_total));
    Check(read_bytes_at_30 > 0 &&
              published_at_30 - read_bytes_at_30 <= chunk + 64 * 1024,
          "index_keeps_up_with_publish",
          "lag=" + std::to_string(published_at_30 - read_bytes_at_30));
    Check(snapshot.complete, "incremental_complete");
    Check(snapshot.stable_frames == expected, "incremental_exact_total",
          "stable=" + std::to_string(snapshot.stable_frames) +
              " expected=" + std::to_string(expected));
    Check(snapshot.index.duplicate_pts_count() == 0, "no_duplicate_pts");
    Check(stats.read_bytes > 0 && read_ratio > 0.0 && read_ratio <= 2.0,
          "no_repeated_full_scan", "ratio=" + std::to_string(read_ratio));
    Check(stats.max_step_ms <= 200.0, "incremental_step_bound",
          "max_step_ms=" + std::to_string(stats.max_step_ms));
  }

  // 波形渐进：独立音频按 128KB 增长，bucket 只追加。
  const QString audio_path = FindAudio(video_path);
  if (!audio_path.isEmpty()) {
    lapian::audio::AudioFormat audio_format;
    audio_format.sample_rate = 48000;
    audio_format.channels = 1;
    lapian::audio::AudioDecoder audio_decoder;
    std::string audio_error;
    const bool audio_opened =
        audio_decoder.Open(audio_path.toUtf8().constData(), -1, audio_format,
                           &audio_error);
    Check(audio_opened, "audio_probe", audio_error);
    if (audio_opened && audio_decoder.info().duration_us > 0) {
      const int audio_stream_index = audio_decoder.info().stream_index;
      const double audio_duration_s =
          static_cast<double>(audio_decoder.info().duration_us) / 1.0e6;
      audio_decoder.Close();
      const qint64 audio_total = QFileInfo(audio_path).size();
      std::int64_t video_start_us = 0;
      {
        // 用真实首帧 PTS 求 av_offset（DASH 视频首帧 21ms，容器 start_time
        // 可能是 0）；波形映射与产品 AudioEngine 使用同一个偏移口径。
        lapian::media::VideoDecoder video_decoder;
        std::string decode_error;
        if (video_decoder.Open(video_path.toUtf8().constData(),
                               video_probe.info.video.stream_index, false,
                               &decode_error)) {
          lapian::media::DecodedFrameInfo first_frame;
          if (video_decoder.DecodeFirstFrame(&first_frame, &decode_error)) {
            video_start_us = first_frame.pts_us;
          }
          video_decoder.Close();
        }
      }
      const double offset_seconds =
          -static_cast<double>(video_start_us) / 1.0e6;
      auto audio_source =
          std::make_shared<lapian::online::GrowingSource>(audio_total);
      const qint64 audio_chunk = 128 * 1024;
      audio_source->Publish(std::min<qint64>(audio_chunk, audio_total));
      lapian::audio::WaveformGenerator generator;
      const bool started = generator.StartGrowing(
          audio_source, audio_path.toUtf8().constData(), audio_stream_index,
          offset_seconds);
      Check(started, "waveform_started");
      if (started) {
        std::vector<int> counts;
        std::vector<lapian::audio::WaveformBucket> early;
        bool captured_early = false;
        for (qint64 target = audio_chunk * 2;
             target <= audio_total + audio_chunk; target += audio_chunk) {
          audio_source->Publish(std::min(target, audio_total));
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
          const auto data = generator.data();
          const int count = data ? static_cast<int>(data->bucket_count()) : 0;
          if (counts.empty() || counts.back() != count) {
            counts.push_back(count);
          }
          if (!captured_early && count > 4000) {
            data->CopyRange(0, 4000, &early);
            captured_early = true;
          }
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline &&
               generator.status().state !=
                   lapian::audio::WaveformState::kReady) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto data = generator.data();
        const int final_count =
            data ? static_cast<int>(data->bucket_count()) : 0;
        const lapian::audio::WaveformStatus status = generator.status();
        bool monotonic = true;
        int intermediate = 0;
        for (std::size_t i = 1; i < counts.size(); ++i) {
          if (counts[i] < counts[i - 1]) {
            monotonic = false;
          }
          if (counts[i] > 0) {
            ++intermediate;
          }
        }
        std::vector<lapian::audio::WaveformBucket> late_early;
        if (captured_early && data) {
          data->CopyRange(0, 4000, &late_early);
        }
        bool prefix_unchanged =
            captured_early && early.size() == late_early.size();
        if (prefix_unchanged) {
          for (std::size_t i = 0; i < early.size(); ++i) {
            if (early[i].min != late_early[i].min ||
                early[i].max != late_early[i].max) {
              prefix_unchanged = false;
              break;
            }
          }
        }
        const int expected_buckets =
            static_cast<int>(audio_duration_s * 200.0);
        const double offset_frames = std::abs(offset_seconds) * fps;
        std::fprintf(
            stderr,
            "online_index_waveform state=%d counts=%zu final=%d expected=%d "
            "prefix_unchanged=%d monotonic=%d intermediate=%d offset_s=%.3f "
            "offset_frames=%.3f\n",
            static_cast<int>(status.state), counts.size(), final_count,
            expected_buckets, prefix_unchanged ? 1 : 0, monotonic ? 1 : 0,
            intermediate, offset_seconds, offset_frames);
        std::fflush(stderr);
        Check(counts.size() >= 3 && intermediate >= 2, "waveform_progressive",
              "samples=" + std::to_string(counts.size()));
        Check(monotonic, "waveform_monotonic");
        Check(prefix_unchanged, "waveform_prefix_unchanged");
        Check(status.state == lapian::audio::WaveformState::kReady,
              "waveform_ready");
        Check(final_count >= static_cast<int>(expected_buckets * 0.97),
              "waveform_covers_duration",
              "final=" + std::to_string(final_count) +
                  " expected=" + std::to_string(expected_buckets));
        Check(std::abs(offset_seconds) * fps <= 1.0,
              "waveform_offset_within_1_frame",
              "offset_frames=" + std::to_string(offset_frames));
        Check(data && std::abs(data->offset_seconds() - offset_seconds) < 1e-6,
              "waveform_offset_value");
        // 已生成区间的末端映射回帧刻度：bucket/200 + av_offset 应落在音频/视频
        // 容器时长 ±1 帧内（不是只看 offset 本身）。
        const double waveform_end_seconds =
            static_cast<double>(final_count) /
                static_cast<double>(lapian::audio::WaveformData::kBucketsPerSecond) +
            offset_seconds;
        Check(std::abs(waveform_end_seconds - audio_duration_s) <= 1.0 / fps,
              "waveform_end_within_1_frame",
              "end=" + std::to_string(waveform_end_seconds) +
                  " duration=" + std::to_string(audio_duration_s) +
                  " frame=" + std::to_string(1.0 / fps));
        generator.Cancel();
      }
    } else {
      std::fprintf(stderr, "online_index_waveform_skip reason=no_audio\n");
    }
  }

  std::fprintf(stderr, "online_index_result checks=%d failures=%d verdict=%s\n",
               g_checks, g_failures, g_failures == 0 ? "pass" : "fail");
  return g_failures == 0 ? 0 : 1;
}
