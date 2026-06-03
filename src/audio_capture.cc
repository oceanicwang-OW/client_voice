#include "audio_capture.h"

#include <cmath>

#include "audio_constants.h"

// miniaudio 是单头库; 实现宏在本 TU 定义一次 (整个项目唯一)。
#if __has_include("miniaudio.h")
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#define VOICE_HAVE_MINIAUDIO 1
#endif

namespace voice {

struct AudioCapture::Impl {
  // 环形缓冲容量: 约 2 秒 16k 样本，足够吸收处理线程抖动。
  SpscRingBuffer<int16_t> ring{kSampleRate * 2};
#if defined(VOICE_HAVE_MINIAUDIO)
  ma_device device{};
  bool device_inited = false;
#endif
};

#if defined(VOICE_HAVE_MINIAUDIO)
namespace {
// 采集回调 (实时线程): 只算电平 + 写环形缓冲，绝不阻塞 (PRD §10)。
void CaptureCallback(ma_device* device, void* /*output*/, const void* input,
                     ma_uint32 frame_count) {
  auto* self = static_cast<AudioCapture*>(device->pUserData);
  const auto* samples = static_cast<const int16_t*>(input);
  if (!samples) return;

  double sum_sq = 0.0;
  for (ma_uint32 i = 0; i < frame_count; ++i) {
    const double s = samples[i] / 32768.0;
    sum_sq += s * s;
  }
  self->set_input_level_internal(
      static_cast<float>(std::sqrt(sum_sq / (frame_count ? frame_count : 1))));
  self->ring_write_internal(samples, frame_count);
}
}  // namespace
#endif

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}
AudioCapture::~AudioCapture() { Stop(); }

bool AudioCapture::Start() {
#if defined(VOICE_HAVE_MINIAUDIO)
  if (running_.load()) return true;
  ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
  cfg.capture.format = ma_format_s16;
  cfg.capture.channels = kChannels;
  cfg.sampleRate = kSampleRate;  // miniaudio 负责重采样到 16k/mono。
  cfg.dataCallback = CaptureCallback;
  cfg.pUserData = this;
  if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) return false;
  impl_->device_inited = true;
  if (ma_device_start(&impl_->device) != MA_SUCCESS) return false;
  running_.store(true);
  return true;
#else
  // TODO(M2): 集成 miniaudio.h 后启用真实采集 (见 README 依赖获取)。
  return false;
#endif
}

void AudioCapture::Stop() {
  if (!running_.exchange(false)) return;
#if defined(VOICE_HAVE_MINIAUDIO)
  if (impl_->device_inited) {
    ma_device_uninit(&impl_->device);
    impl_->device_inited = false;
  }
#endif
  impl_->ring.Clear();
  input_level_.store(0.0f);
}

size_t AudioCapture::Read(Pcm16* out, size_t max_samples) {
  out->resize(max_samples);
  const size_t n = impl_->ring.Read(out->data(), max_samples);
  out->resize(n);
  return n;
}

// 仅供回调使用的内部桥接 (声明见 .h 中的 friend 替代方案: 这里用公有薄封装)。
void AudioCapture::set_input_level_internal(float level) {
  input_level_.store(level);
}
void AudioCapture::ring_write_internal(const int16_t* data, size_t count) {
  impl_->ring.Write(data, count);
}

}  // namespace voice
