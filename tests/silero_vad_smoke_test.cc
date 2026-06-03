// M4 Silero VAD 集成烟雾测试。
//
// 加载真实 silero_vad.onnx (ONNX Runtime)，逐 512 窗推理，校验:
//   - 模型成功加载。
//   - 推理不抛异常，返回概率恒在 [0,1]。
//   - 纯静音判为低概率 (< 0.5)。
// 注: 合成信号不保证被判为"语音"，真实语音准确率需戴耳机人工验证 (AC-3/AC-4
//     的语音准确性)。本测试只确认 ONNX 集成链路正确可用。
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio_constants.h"
#include "vad.h"

#ifndef VOICE_SILERO_MODEL_PATH
#define VOICE_SILERO_MODEL_PATH "models/silero_vad.onnx"
#endif

int main() {
  voice::VadDetector vad;
  if (!vad.Load(VOICE_SILERO_MODEL_PATH)) {
    std::printf("FAIL: could not load model at %s\n", VOICE_SILERO_MODEL_PATH);
    return 1;
  }

  std::vector<int16_t> frame(voice::kVadWindowSamples, 0);
  float max_silence_prob = 0.0f;
  bool all_valid = true;

  // 30 个静音窗。
  for (int i = 0; i < 30; ++i) {
    vad.Push(frame.data());
    const float p = vad.last_prob();
    all_valid = all_valid && (p >= 0.0f && p <= 1.0f);
    max_silence_prob = p > max_silence_prob ? p : max_silence_prob;
  }

  // 一段宽带噪声: 仅确认推理稳定、概率合法 (不对其类别下断言)。
  int phase = 1;
  for (int i = 0; i < 30; ++i) {
    for (int j = 0; j < voice::kVadWindowSamples; ++j) {
      phase = phase * 1103515245 + 12345;
      frame[j] = static_cast<int16_t>((phase >> 16) % 8000);
    }
    vad.Push(frame.data());
    const float p = vad.last_prob();
    all_valid = all_valid && (p >= 0.0f && p <= 1.0f);
  }

  if (!all_valid) {
    std::printf("FAIL: probability out of [0,1]\n");
    return 1;
  }
  if (max_silence_prob >= 0.5f) {
    std::printf("FAIL: silence misclassified as speech (prob=%.3f)\n",
                max_silence_prob);
    return 1;
  }

  std::printf("PASS: M4 Silero integration (model loaded, inference valid, "
              "silence prob max=%.4f)\n", max_silence_prob);
  return 0;
}
