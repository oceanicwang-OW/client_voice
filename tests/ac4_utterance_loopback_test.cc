// AC-4 语音闭环 (PRD §13)。
//
// 通过 ChatController::FeedAudioForTest 驱动真实链路 (能量 VAD 后端，无需声卡):
//   静音 -> 一句话(响亮正弦) -> 静音
// 校验消息区出现:
//   - "VAD: speech start" / "VAD: speech end"
//   - "Voice: x.xs utterance looped back"  (Utterance Loopback, §7.1)
// 即用户说完一句，整段经上行->Loopback->下行被"回复"回来。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "audio_constants.h"
#include "chat_controller.h"

namespace {
void Append(voice::Pcm16* buf, int ms, bool voiced, int* phase) {
  const int n = voice::kSampleRate * ms / 1000;
  for (int i = 0; i < n; ++i) {
    int16_t s = 0;
    if (voiced) {
      double t = static_cast<double>(*phase) / voice::kSampleRate;
      s = static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265 * 220.0 * t));
    }
    ++(*phase);
    buf->push_back(s);
  }
}
}  // namespace

int main() {
  voice::ChatController c;
  if (!c.Init()) {
    std::printf("FAIL: Init\n");
    return 1;
  }
  c.settings().realtime_monitor.store(false);   // Utterance Loopback 模式。
  c.settings().min_silence_ms.store(600);
  c.settings().loopback_delay_ms.store(0);
  c.settings().vad_threshold.store(0.5f);

  // 构造: 300ms 静音 + 800ms 语音 + 900ms 静音 (尾随静音 > 600ms 触发句末)。
  voice::Pcm16 pcm;
  int phase = 0;
  Append(&pcm, 300, false, &phase);
  Append(&pcm, 800, true, &phase);
  Append(&pcm, 900, false, &phase);

  // 分块喂入，模拟采集回调的任意块长。
  const size_t kChunk = 1024;
  for (size_t off = 0; off < pcm.size(); off += kChunk) {
    const size_t n = std::min(kChunk, pcm.size() - off);
    c.FeedAudioForTest(voice::Pcm16(pcm.begin() + off, pcm.begin() + off + n));
  }

  bool saw_start = false, saw_end = false, saw_voice = false;
  for (const auto& m : c.log().Drain()) {
    if (m.kind == voice::MessageKind::kLog && m.text.find("speech start") != std::string::npos)
      saw_start = true;
    if (m.kind == voice::MessageKind::kLog && m.text.find("speech end") != std::string::npos)
      saw_end = true;
    if (m.kind == voice::MessageKind::kVoice && m.text.find("looped back") != std::string::npos)
      saw_voice = true;
  }
  c.Shutdown();

  if (saw_start && saw_end && saw_voice) {
    std::printf("PASS: AC-4 utterance loopback (speech start/end + Voice looped back)\n");
    return 0;
  }
  std::printf("FAIL: AC-4 (start=%d end=%d voice=%d)\n", saw_start, saw_end, saw_voice);
  return 1;
}
