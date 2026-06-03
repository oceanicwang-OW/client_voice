// AC-3 端点检测 (PRD §13)。
//
// 直接驱动 VadDetector 状态机 (能量 VAD 后端，确定性): 喂 静音 -> 语音 -> 静音
// 的 512(32ms) 窗序列，校验:
//   - 连续 >=2 个有声窗后触发 kSpeechStart (句首确认，抑制瞬时噪声)。
//   - 语音期间不重复触发。
//   - 尾随静音累计达 min_silence(600ms) 后触发 kSpeechEnd。
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio_constants.h"
#include "vad.h"

namespace {
// 生成一个 512 样本窗: 静音 (amp=0) 或响亮正弦 (能量 VAD 判为有声)。
void FillWindow(int16_t* w, bool voiced, int phase) {
  for (int i = 0; i < voice::kVadWindowSamples; ++i) {
    if (!voiced) {
      w[i] = 0;
    } else {
      double t = static_cast<double>(phase + i) / voice::kSampleRate;
      w[i] = static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265 * 220.0 * t));
    }
  }
}
bool Check(bool cond, const char* msg) {
  if (!cond) std::printf("FAIL: %s\n", msg);
  return cond;
}
}  // namespace

int main() {
  voice::VadDetector vad;
  vad.Load("");  // 能量 VAD 无需模型。
  vad.SetThreshold(0.5f);
  vad.SetMinSilenceMs(600);

  std::vector<int16_t> win(voice::kVadWindowSamples);
  int phase = 0;
  int start_at = -1, end_at = -1, starts = 0, ends = 0;

  auto push = [&](bool voiced) -> voice::VadEvent {
    FillWindow(win.data(), voiced, phase);
    phase += voice::kVadWindowSamples;
    return vad.Push(win.data());
  };

  int idx = 0;
  // 10 个静音窗: 不应触发。
  for (int i = 0; i < 10; ++i, ++idx) {
    if (push(false) != voice::VadEvent::kNone) {
      std::printf("FAIL: spurious event during initial silence at win %d\n", idx);
      return 1;
    }
  }
  // 40 个语音窗 (~1.28s): 应在前两窗内触发一次 start。
  for (int i = 0; i < 40; ++i, ++idx) {
    voice::VadEvent ev = push(true);
    if (ev == voice::VadEvent::kSpeechStart) { ++starts; start_at = idx; }
    if (ev == voice::VadEvent::kSpeechEnd) { ++ends; }
  }
  // 30 个静音窗 (~0.96s > 600ms): 应触发一次 end。
  for (int i = 0; i < 30; ++i, ++idx) {
    voice::VadEvent ev = push(false);
    if (ev == voice::VadEvent::kSpeechEnd) { ++ends; end_at = idx; }
    if (ev == voice::VadEvent::kSpeechStart) { ++starts; }
  }

  const int kWinMs = voice::kVadWindowSamples * 1000 / voice::kSampleRate;  // 32
  bool ok = true;
  ok &= Check(starts == 1, "exactly one speech-start");
  ok &= Check(ends == 1, "exactly one speech-end");
  // start 应在语音开始后约 2 窗内 (idx 10 起，<= 12)。
  ok &= Check(start_at >= 10 && start_at <= 12, "speech-start within 2 voiced windows");
  // end 应在静音开始 (idx 50) 后约 600ms (~19 窗) 触发。
  const int silence_windows = (end_at - 50 + 1) * kWinMs;
  ok &= Check(end_at >= 50, "speech-end during trailing silence");
  ok &= Check(silence_windows >= 600 && silence_windows <= 760,
              "speech-end after ~600ms trailing silence");
  if (!ok) {
    std::printf("  (start_at=%d end_at=%d trailing=%dms)\n", start_at, end_at,
                silence_windows);
    return 1;
  }

  std::printf("PASS: AC-3 endpoint detection (start@win%d, end@win%d, ~%dms silence)\n",
              start_at, end_at, silence_windows);
  return 0;
}
