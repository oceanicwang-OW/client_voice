#include "audio_playback.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

#include "audio_constants.h"

// 注意: MINIAUDIO_IMPLEMENTATION 已在 audio_capture.cc 定义，这里只包含声明。
#if __has_include("miniaudio.h")
#include "miniaudio.h"
#define VOICE_HAVE_MINIAUDIO 1
#endif

namespace voice {

namespace {
constexpr size_t kPrebufferSamples =
    static_cast<size_t>(kSampleRate) * kJitterPrebufferMsMax / 1000;
}  // namespace

struct AudioPlayback::Impl {
  std::mutex mu;
  std::deque<int16_t> jitter;     // jitter buffer (解码后样本)。
  bool started_output = false;    // 是否已攒够预缓冲起播。
  IAudioProcessor* render_sink = nullptr;
  std::vector<int16_t> ref_frame;  // 累积满 10ms 再喂 AEC 参考。
#if defined(VOICE_HAVE_MINIAUDIO)
  ma_device device{};
  bool device_inited = false;
#endif
};

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
  return ma_device_start(&impl_->device) == MA_SUCCESS;
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
  std::lock_guard<std::mutex> lk(impl_->mu);
  impl_->jitter.insert(impl_->jitter.end(), pcm.begin(), pcm.end());
}

void AudioPlayback::Flush() {
  std::lock_guard<std::mutex> lk(impl_->mu);
  impl_->jitter.clear();
  impl_->started_output = false;
  playing_.store(false);
}

void AudioPlayback::SetRenderRefSink(IAudioProcessor* apm) {
  std::lock_guard<std::mutex> lk(impl_->mu);
  impl_->render_sink = apm;
}

void AudioPlayback::fill_output_internal(int16_t* output, size_t frame_count) {
  std::lock_guard<std::mutex> lk(impl_->mu);
  // 攒够预缓冲再起播，避免欠载 (PRD §8 jitter buffer)。
  if (!impl_->started_output) {
    if (impl_->jitter.size() < kPrebufferSamples) {
      std::fill(output, output + frame_count, int16_t{0});
      return;
    }
    impl_->started_output = true;
    playing_.store(true);
  }

  for (size_t i = 0; i < frame_count; ++i) {
    int16_t s = 0;
    if (!impl_->jitter.empty()) {
      s = impl_->jitter.front();
      impl_->jitter.pop_front();
    }
    output[i] = s;
    // 累积 10ms (160 samples) 后喂给 AEC 参考 (PRD §15.2)。
    if (impl_->render_sink) {
      impl_->ref_frame.push_back(s);
      if (impl_->ref_frame.size() == kApmFrameSamples) {
        impl_->render_sink->ProcessRenderRef(impl_->ref_frame.data());
        impl_->ref_frame.clear();
      }
    }
  }

  if (impl_->jitter.empty()) {
    impl_->started_output = false;
    playing_.store(false);
  }
}

}  // namespace voice
