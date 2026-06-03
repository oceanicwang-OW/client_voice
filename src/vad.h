// VAD 端点检测 (PRD §9.4) —— 基于 Silero VAD (onnxruntime)。
//
// 严格按模型版本要求的样本数喂入 (v4/v5 为 512@16k)，尺寸不对会报错或输出
// 无意义 (PRD §15.6)。内部状态机: IDLE / SPEAKING + trailing-silence 计数。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace voice {

enum class VadEvent { kNone, kSpeechStart, kSpeechEnd };

class VadDetector {
 public:
  VadDetector();
  ~VadDetector();

  VadDetector(const VadDetector&) = delete;
  VadDetector& operator=(const VadDetector&) = delete;

  // 加载 ONNX 模型 (启用 Silero 时)。passthrough/能量 VAD 实现可忽略路径。
  bool Load(const std::string& onnx_model_path);

  // 送入 512 samples(32ms)，返回是否触发起/止事件。
  VadEvent Push(const int16_t* frame_512);

  void SetThreshold(float t);
  void SetMinSilenceMs(int ms);
  void Reset();

  float last_prob() const { return last_prob_.load(); }  // 供 UI 显示。

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<float> last_prob_{0.0f};
};

}  // namespace voice
