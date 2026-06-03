#include "audio_processor.h"

#include "audio_constants.h"

#if defined(VOICE_ENABLE_WEBRTC)
#include <array>
// pulseaudio standalone webrtc-audio-processing 的公共头 (include dir 由
// find_package 提供)。命名空间 webrtc / rtc。
#include <modules/audio_processing/include/audio_processing.h>
#endif

namespace voice {

namespace {

// 直通实现 (PRD §11): 不做任何处理，先跑通链路。
class PassthroughApm : public IAudioProcessor {
 public:
  void ProcessCapture(int16_t* /*frame_160*/) override {}
  void ProcessRenderRef(const int16_t* /*frame_160*/) override {}
  void SetAecEnabled(bool /*enabled*/) override {}
  void SetNsEnabled(bool /*enabled*/) override {}
};

#if defined(VOICE_ENABLE_WEBRTC)

// 真实 WebRTC APM 适配 (AEC3 + NS + AGC)。
//
// 关键约定 (PRD §8/§15.2): 按 10ms(160@16k) 帧处理; 播放链路的每一帧 render
// 通过 ProcessRenderRef 喂入，与 capture 帧时间对齐，AEC 才有效。
class WebrtcApm : public IAudioProcessor {
 public:
  WebrtcApm() {
    apm_ = webrtc::AudioProcessingBuilder().Create();
    cfg_.echo_canceller.enabled = true;
    cfg_.echo_canceller.mobile_mode = false;  // 桌面用全 AEC3。
    cfg_.noise_suppression.enabled = true;
    cfg_.noise_suppression.level =
        webrtc::AudioProcessing::Config::NoiseSuppression::kHigh;
    cfg_.gain_controller1.enabled = true;
    cfg_.gain_controller1.mode =
        webrtc::AudioProcessing::Config::GainController1::kAdaptiveDigital;
    cfg_.high_pass_filter.enabled = true;
    if (apm_) apm_->ApplyConfig(cfg_);
  }

  bool valid() const { return apm_ != nullptr; }

  void ProcessCapture(int16_t* frame_160) override {
    if (!apm_) return;
    // 近端处理: 原地输出 (src==dest 同缓冲)。
    apm_->set_stream_delay_ms(stream_delay_ms_);
    apm_->ProcessStream(frame_160, stream_cfg_, stream_cfg_, frame_160);
  }

  void ProcessRenderRef(const int16_t* frame_160) override {
    if (!apm_) return;
    // 远端参考: ProcessReverseStream 需要可写输出缓冲，用 scratch。
    apm_->ProcessReverseStream(frame_160, stream_cfg_, stream_cfg_,
                               scratch_.data());
  }

  void SetAecEnabled(bool enabled) override {
    cfg_.echo_canceller.enabled = enabled;
    if (apm_) apm_->ApplyConfig(cfg_);
  }
  void SetNsEnabled(bool enabled) override {
    cfg_.noise_suppression.enabled = enabled;
    if (apm_) apm_->ApplyConfig(cfg_);
  }

 private:
  rtc::scoped_refptr<webrtc::AudioProcessing> apm_;
  webrtc::AudioProcessing::Config cfg_;
  webrtc::StreamConfig stream_cfg_{kSampleRate, kChannels};
  std::array<int16_t, kApmFrameSamples> scratch_{};
  int stream_delay_ms_ = 0;  // Loopback 近实时, 参考与采集基本对齐。
};

#endif  // VOICE_ENABLE_WEBRTC

}  // namespace

std::unique_ptr<IAudioProcessor> CreatePassthroughApm() {
  return std::make_unique<PassthroughApm>();
}

std::unique_ptr<IAudioProcessor> CreateWebrtcApm() {
#if defined(VOICE_ENABLE_WEBRTC)
  auto apm = std::make_unique<WebrtcApm>();
  if (apm->valid()) return apm;
  // 创建失败则回退直通，保证链路仍可运行 (PRD §11)。
  return CreatePassthroughApm();
#else
  // 未编入 WebRTC: 直通 (戴耳机测试避免自激, PRD §11)。
  return CreatePassthroughApm();
#endif
}

}  // namespace voice
