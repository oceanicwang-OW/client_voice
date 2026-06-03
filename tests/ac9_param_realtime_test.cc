// AC-9 参数实时生效 (PRD §13)。
//
// 同一段音频 (语音 - 450ms 停顿 - 语音 - 长静音)，仅改变"句末静音时长"设置:
//   - min_silence=300ms: 450ms 停顿 > 阈值 -> 在停顿处断句 -> 2 句 -> 2 次回环。
//   - min_silence=600ms: 450ms 停顿 < 阈值 -> 不断句 -> 合为 1 句 -> 1 次回环。
// 回环条数随阈值变化，即证明断句灵敏度随滑块实时变化。
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "audio_constants.h"
#include "chat_controller.h"

namespace {
void AppendTone(voice::Pcm16* buf, int ms, bool voiced, int* phase) {
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

// 用给定 min_silence 跑一遍，返回回环句数。
int CountUtterances(int min_silence_ms) {
  voice::ChatController c;
  if (!c.Init()) return -1;
  c.settings().realtime_monitor.store(false);
  c.settings().vad_threshold.store(0.5f);
  c.settings().loopback_delay_ms.store(0);
  c.settings().min_silence_ms.store(min_silence_ms);

  voice::Pcm16 pcm;
  int phase = 0;
  AppendTone(&pcm, 100, false, &phase);
  AppendTone(&pcm, 500, true, &phase);
  AppendTone(&pcm, 450, false, &phase);  // 判别性停顿。
  AppendTone(&pcm, 500, true, &phase);
  AppendTone(&pcm, 800, false, &phase);  // 尾随静音 (> 两种阈值) 收尾。

  const size_t kChunk = 1024;
  for (size_t off = 0; off < pcm.size(); off += kChunk) {
    const size_t n = (kChunk < pcm.size() - off) ? kChunk : (pcm.size() - off);
    c.FeedAudioForTest(voice::Pcm16(pcm.begin() + off, pcm.begin() + off + n));
  }

  int voices = 0;
  for (const auto& m : c.log().Drain())
    if (m.kind == voice::MessageKind::kVoice &&
        m.text.find("looped back") != std::string::npos)
      ++voices;
  c.Shutdown();
  return voices;
}
}  // namespace

int main() {
  const int short_sil = CountUtterances(300);
  const int long_sil = CountUtterances(600);
  std::printf("min_silence=300ms -> %d utterance(s); 600ms -> %d utterance(s)\n",
              short_sil, long_sil);

  if (short_sil == 2 && long_sil == 1) {
    std::printf("PASS: AC-9 params take effect in real time (endpointing changed)\n");
    return 0;
  }
  std::printf("FAIL: AC-9 (expected 2 and 1)\n");
  return 1;
}
