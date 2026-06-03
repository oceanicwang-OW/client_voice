#include "audio_playback.h"

#include <algorithm>
#include <array>
#include <atomic>

#include "audio_constants.h"
#include "ring_buffer.h"

// 注意: MINIAUDIO_IMPLEMENTATION 已在 audio_capture.cc 定义，这里只包含声明。
#if __has_include("miniaudio.h")
#include "miniaudio.h"
#define VOICE_HAVE_MINIAUDIO 1
#endif

namespace voice {

namespace {
constexpr size_t kPrebufferSamples =
    static_cast<size_t>(kSampleRate) * kJitterPrebufferMsMax / 1000;
constexpr size_t kPlaybackBufferSamples =
    static_cast<size_t>(kSampleRate) * 10;  // 10 seconds of 16 kHz mono PCM.
}  // namespace

struct AudioPlayback::Impl {
  SpscRingBuffer<int16_t> jitter{kPlaybackBufferSamples};
  std::atomic<bool> started_output{false};
  std::atomic<bool> flush_requested{false};
  std::atomic<size_t> audible_tail_samples{0};
  std::atomic<IAudioProcessor*> render_sink{nullptr};
  std::array<int16_t, kApmFrameSamples> ref_frame{};
  size_t ref_fill = 0;
#if defined(VOICE_HAVE_MINIAUDIO)
  ma_device device{};
  bool device_inited = false;
#endif

  void ProcessFlushIfRequested();
  void FeedRenderRef(int16_t sample);
  void MarkDeviceBufferQueued(size_t frame_count);
  void DecayAudibleTail(size_t frame_count);
};

void AudioPlayback::Impl::ProcessFlushIfRequested() {
  if (!flush_requested.exchange(false)) return;
  jitter.DiscardReadable();
  started_output.store(false);
  audible_tail_samples.store(0);
  ref_fill = 0;
}

void AudioPlayback::Impl::FeedRenderRef(int16_t sample) {
  IAudioProcessor* sink = render_sink.load(std::memory_order_acquire);
  if (!sink) return;
  ref_frame[ref_fill++] = sample;
  if (ref_fill == ref_frame.size()) {
    sink->ProcessRenderRef(ref_frame.data());
    ref_fill = 0;
  }
}

void AudioPlayback::Impl::MarkDeviceBufferQueued(size_t frame_count) {
  audible_tail_samples.store(frame_count, std::memory_order_release);
}

void AudioPlayback::Impl::DecayAudibleTail(size_t frame_count) {
  size_t tail = audible_tail_samples.load(std::memory_order_acquire);
  while (tail > 0) {
    const size_t next = tail > frame_count ? tail - frame_count : 0;
    if (audible_tail_samples.compare_exchange_weak(
            tail, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
      break;
    }
  }
}

#if defined(VOICE_HAVE_MINIAUDIO)
namespace {
// 播放回调 (实时线程): 从 jitter buffer 取数据，不足填静音 (PRD §9.7)。
void PlaybackCallback(ma_device* device, void* output, const void* /*input*/,
                      ma_uint32 frame_count) {
  auto* self = static_cast<AudioPlayback*>(device->pUserData);
  self->fill_output_internal(static_cast<int16_t*>(output), frame_count);
}
}  // namespace
#endif

AudioPlayback::AudioPlayback() : impl_(std::make_unique<Impl>()) {}
AudioPlayback::~AudioPlayback() { Stop(); }

bool AudioPlayback::Start() {
#if defined(VOICE_HAVE_MINIAUDIO)
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_s16;
  cfg.playback.channels = kChannels;
  cfg.sampleRate = kSampleRate;
  cfg.dataCallback = PlaybackCallback;
  cfg.pUserData = this;
  if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) return false;
  impl_->device_inited = true;
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
    impl_->device_inited = false;
    return false;
  }
  return true;
#else
  // TODO(M2): 集成 miniaudio.h 后启用真实播放。
  return false;
#endif
}

void AudioPlayback::Stop() {
#if defined(VOICE_HAVE_MINIAUDIO)
  if (impl_->device_inited) {
    ma_device_uninit(&impl_->device);
    impl_->device_inited = false;
  }
#endif
  Flush();
}

void AudioPlayback::Enqueue(const Pcm16& pcm) {
  impl_->jitter.Write(pcm.data(), pcm.size());
}

void AudioPlayback::Flush() {
  impl_->flush_requested.store(true, std::memory_order_release);
  impl_->started_output.store(false, std::memory_order_release);
  impl_->audible_tail_samples.store(0, std::memory_order_release);
  playing_.store(false);
}

void AudioPlayback::SetRenderRefSink(IAudioProcessor* apm) {
  impl_->render_sink.store(apm, std::memory_order_release);
}

void AudioPlayback::fill_output_internal(int16_t* output, size_t frame_count) {
  impl_->ProcessFlushIfRequested();
  // 攒够预缓冲再起播，避免欠载 (PRD §8 jitter buffer)。
  if (!impl_->started_output.load(std::memory_order_acquire)) {
    if (impl_->jitter.Size() < kPrebufferSamples) {
      std::fill(output, output + frame_count, int16_t{0});
      impl_->DecayAudibleTail(frame_count);
      playing_.store(impl_->audible_tail_samples.load(std::memory_order_acquire) > 0);
      return;
    }
    impl_->started_output.store(true, std::memory_order_release);
    playing_.store(true);
  }

  size_t samples_read = 0;
  for (size_t i = 0; i < frame_count; ++i) {
    int16_t s = 0;
    samples_read += impl_->jitter.Read(&s, 1);
    output[i] = s;
    impl_->FeedRenderRef(s);
  }

  if (samples_read > 0) {
    impl_->MarkDeviceBufferQueued(frame_count);
    playing_.store(true);
  } else {
    impl_->DecayAudibleTail(frame_count);
    playing_.store(impl_->audible_tail_samples.load(std::memory_order_acquire) > 0);
  }
  if (impl_->jitter.Size() == 0) {
    impl_->started_output.store(false, std::memory_order_release);
  }
}

}  // namespace voice
