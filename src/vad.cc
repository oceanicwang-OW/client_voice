#include "vad.h"

#include <cmath>

#include "audio_constants.h"

#if defined(VOICE_ENABLE_SILERO)
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <onnxruntime_cxx_api.h>
#endif

namespace voice {

namespace {
enum class State { kIdle, kSpeaking };

// 每个 VAD 窗 (512 samples @16k = 32ms) 的时长。
constexpr int kWindowMs = kVadWindowSamples * 1000 / kSampleRate;  // 32
}  // namespace

struct VadDetector::Impl {
  State state = State::kIdle;
  float threshold = kVadDefaultThreshold;
  int min_silence_ms = kVadDefaultMinSilenceMs;
  int voiced_windows = 0;     // 句首确认计数。
  int silence_ms = 0;         // 句末静音累计。
  bool loaded = false;

#if defined(VOICE_ENABLE_SILERO)
  // Silero VAD v5: 输入 input[1,512] + state[2,1,128] + sr(scalar int64)，
  // 输出 output[1,1] (语音概率) + stateN[2,1,128]。
  // ERROR 级别: 抑制模型加载时大量无害的 initializer 清理告警。
  Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "silero_vad"};
  Ort::SessionOptions opts;
  std::unique_ptr<Ort::Session> session;
  Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::vector<std::string> in_names_str, out_names_str;
  std::vector<const char*> in_names, out_names;
  std::array<float, 2 * 1 * 128> hc_state{};  // h/c 合并状态，跨帧保持。
  int64_t sr = kSampleRate;

  bool LoadModel(const std::string& path) {
    try {
      opts.SetIntraOpNumThreads(1);
      opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
      std::wstring wpath(path.begin(), path.end());
      session = std::make_unique<Ort::Session>(env, wpath.c_str(), opts);
#else
      session = std::make_unique<Ort::Session>(env, path.c_str(), opts);
#endif
      Ort::AllocatorWithDefaultOptions alloc;
      for (size_t i = 0; i < session->GetInputCount(); ++i)
        in_names_str.push_back(session->GetInputNameAllocated(i, alloc).get());
      for (size_t i = 0; i < session->GetOutputCount(); ++i)
        out_names_str.push_back(session->GetOutputNameAllocated(i, alloc).get());
      for (auto& s : in_names_str) in_names.push_back(s.c_str());
      for (auto& s : out_names_str) out_names.push_back(s.c_str());
      hc_state.fill(0.0f);
      return true;
    } catch (const Ort::Exception&) {
      session.reset();
      return false;
    }
  }

  // 按输入名称组织张量 (input / state / sr)，避免硬编码顺序。
  float Infer(const int16_t* frame) {
    std::array<float, kVadWindowSamples> audio;
    for (int i = 0; i < kVadWindowSamples; ++i) audio[i] = frame[i] / 32768.0f;

    const int64_t in_shape[2] = {1, kVadWindowSamples};
    const int64_t st_shape[3] = {2, 1, 128};
    const int64_t sr_shape[1] = {1};

    std::vector<Ort::Value> inputs;
    for (const auto& name : in_names_str) {
      if (name.find("state") != std::string::npos || name == "h" || name == "c") {
        inputs.push_back(Ort::Value::CreateTensor<float>(
            mem, hc_state.data(), hc_state.size(), st_shape, 3));
      } else if (name.find("sr") != std::string::npos) {
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, &sr, 1, sr_shape, 1));
      } else {  // 音频输入。
        inputs.push_back(Ort::Value::CreateTensor<float>(
            mem, audio.data(), audio.size(), in_shape, 2));
      }
    }

    auto outs = session->Run(Ort::RunOptions{nullptr}, in_names.data(),
                             inputs.data(), inputs.size(), out_names.data(),
                             out_names.size());
    float prob = outs[0].GetTensorMutableData<float>()[0];
    // 回写新状态 (输出里非概率的那个张量)。
    for (size_t i = 1; i < outs.size(); ++i) {
      auto info = outs[i].GetTensorTypeAndShapeInfo();
      if (info.GetElementCount() == hc_state.size()) {
        const float* ns = outs[i].GetTensorMutableData<float>();
        std::copy(ns, ns + hc_state.size(), hc_state.begin());
      }
    }
    return prob;
  }

  void state_reset() { hc_state.fill(0.0f); }
#endif

  // 返回当前窗的语音概率 [0,1]。
  float Prob(const int16_t* frame) {
#if defined(VOICE_ENABLE_SILERO)
    if (!loaded || !session) return 0.0f;
    float p = Infer(frame);
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
#else
    // 能量 VAD 占位 (零模型文件路径, PRD §11)。
    double sum_sq = 0.0;
    for (int i = 0; i < kVadWindowSamples; ++i) {
      const double s = frame[i] / 32768.0;
      sum_sq += s * s;
    }
    const double rms = std::sqrt(sum_sq / kVadWindowSamples);
    float p = static_cast<float>((rms - 0.02) / (0.20 - 0.02));
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
#endif
  }
};

VadDetector::VadDetector() : impl_(std::make_unique<Impl>()) {}
VadDetector::~VadDetector() = default;

bool VadDetector::Load(const std::string& onnx_model_path) {
#if defined(VOICE_ENABLE_SILERO)
  impl_->loaded = impl_->LoadModel(onnx_model_path);
  return impl_->loaded;
#else
  (void)onnx_model_path;
  impl_->loaded = true;
  return true;  // 能量 VAD 无需模型。
#endif
}

VadEvent VadDetector::Push(const int16_t* frame_512) {
  const float prob = impl_->Prob(frame_512);
  last_prob_.store(prob);
  const bool voiced = prob > impl_->threshold;

  if (impl_->state == State::kIdle) {
    if (voiced) {
      if (++impl_->voiced_windows >= kVadSpeechStartWindows) {
        impl_->state = State::kSpeaking;
        impl_->voiced_windows = 0;
        impl_->silence_ms = 0;
        return VadEvent::kSpeechStart;
      }
    } else {
      impl_->voiced_windows = 0;
    }
    return VadEvent::kNone;
  }

  // kSpeaking: 累计 trailing silence，达阈值判句末。
  if (voiced) {
    impl_->silence_ms = 0;
  } else {
    impl_->silence_ms += kWindowMs;
    if (impl_->silence_ms >= impl_->min_silence_ms) {
      impl_->state = State::kIdle;
      impl_->silence_ms = 0;
      return VadEvent::kSpeechEnd;
    }
  }
  return VadEvent::kNone;
}

void VadDetector::SetThreshold(float t) { impl_->threshold = t; }
void VadDetector::SetMinSilenceMs(int ms) { impl_->min_silence_ms = ms; }

void VadDetector::Reset() {
  impl_->state = State::kIdle;
  impl_->voiced_windows = 0;
  impl_->silence_ms = 0;
  last_prob_.store(0.0f);
#if defined(VOICE_ENABLE_SILERO)
  impl_->state_reset();
#endif
}

}  // namespace voice
