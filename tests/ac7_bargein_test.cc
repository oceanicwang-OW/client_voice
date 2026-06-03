// AC-7 Barge-in (PRD §13 / §15.4)。
//
// Part A (确定性, 始终运行): 验证 barge-in 机制本身——
//   - AudioPlayback::Flush() 立即清空 jitter buffer 并停止播放。
//   - LoopbackTransport::CancelPendingAudio() 丢弃待回传音频、保留文本。
// Part B (集成, 依赖声卡, 无设备则 SKIP): 通过真实 ChatController 验证
//   播放中开口 -> kSpeechStart -> Flush + cancel + 日志 "barge-in: playback flushed"。
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "audio_constants.h"
#include "audio_playback.h"
#include "chat_controller.h"
#include "transport.h"

namespace {
bool Check(bool cond, const char* msg) {
  if (!cond) std::printf("FAIL: %s\n", msg);
  return cond;
}
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

// ---- Part A: 机制 ----------------------------------------------------------
bool TestMechanism() {
  bool ok = true;

  // AudioPlayback.Flush。
  {
    voice::AudioPlayback pb;  // 不 Start(): 直接驱动 fill, 无需设备。
    voice::Pcm16 audio(4000, 1000);  // > 预缓冲 1920。
    pb.Enqueue(audio);
    std::vector<int16_t> out(1920);
    pb.fill_output_internal(out.data(), out.size());  // 越过预缓冲 -> 起播。
    ok &= Check(pb.IsPlaying(), "playback playing after prebuffer");
    pb.Flush();
    ok &= Check(!pb.IsPlaying(), "playback stopped after Flush");
    std::vector<int16_t> out2(160, 7);
    pb.fill_output_internal(out2.data(), out2.size());
    bool silent = true;
    for (int16_t s : out2) silent = silent && (s == 0);
    ok &= Check(silent, "buffer cleared (silence) after Flush");
  }

  // LoopbackTransport.CancelPendingAudio: 丢音频、留文本。
  {
    std::atomic<int> audio_cnt{0}, text_cnt{0};
    voice::LoopbackTransport tr;
    tr.SetDelayMs(0);
    tr.on_audio = [&](const voice::OpusPacket&) { ++audio_cnt; };
    tr.on_text = [&](const std::string&) { ++text_cnt; };
    for (int i = 0; i < 5; ++i) tr.SendAudio(voice::OpusPacket(40, 0));
    tr.SendText("keep me");
    tr.CancelPendingAudio();  // 在 Start 前取消待回传音频。
    tr.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    tr.Stop();
    ok &= Check(audio_cnt.load() == 0, "pending audio canceled");
    ok &= Check(text_cnt.load() == 1, "text preserved after cancel");
  }
  return ok;
}

// ---- Part B: ChatController 集成 (确定性, 无需声卡) ------------------------
// 用 PumpPlaybackForTest 模拟播放设备回调拉取，使播放进入播放态，
// 再喂语音触发真实的 barge-in 分支。返回: true=PASS。
bool TestIntegration() {
  using namespace std::chrono_literals;
  voice::ChatController c;
  if (!Check(c.Init(), "controller init")) return false;
  c.settings().realtime_monitor.store(false);
  c.settings().min_silence_ms.store(600);
  c.settings().loopback_delay_ms.store(0);  // 回环立即返回。
  c.settings().vad_threshold.store(0.5f);

  // 1) 喂一句话 -> 句末回环 -> 解码后入播放 jitter buffer。
  voice::Pcm16 utt;
  int phase = 0;
  AppendTone(&utt, 100, false, &phase);
  AppendTone(&utt, 800, true, &phase);
  AppendTone(&utt, 800, false, &phase);  // 尾随静音触发句末 -> 回环。
  const size_t kChunk = 1024;
  for (size_t off = 0; off < utt.size(); off += kChunk) {
    const size_t n = (kChunk < utt.size() - off) ? kChunk : (utt.size() - off);
    c.FeedAudioForTest(voice::Pcm16(utt.begin() + off, utt.begin() + off + n));
  }

  // 2) 等回环音频异步入队，再"拉取"越过预缓冲使其进入播放态。
  std::this_thread::sleep_for(200ms);
  c.PumpPlaybackForTest(4000);  // > 预缓冲 1920。
  if (!Check(c.ui_state().playing.load(), "playback active before barge-in"))
    return false;

  // 3) 播放中开口: 一段语音触发 kSpeechStart -> barge-in。
  c.log().Drain();  // 清掉之前的日志。
  voice::Pcm16 interrupt;
  AppendTone(&interrupt, 200, true, &phase);
  c.FeedAudioForTest(interrupt);

  bool saw_bargein = false;
  for (const auto& m : c.log().Drain())
    if (m.text.find("barge-in") != std::string::npos) saw_bargein = true;
  const bool stopped = !c.ui_state().playing.load();
  c.Shutdown();

  bool ok = true;
  ok &= Check(saw_bargein, "barge-in log emitted during playback");
  ok &= Check(stopped, "playback stopped after barge-in");
  return ok;
}
}  // namespace

int main() {
  if (!TestMechanism()) return 1;
  if (!TestIntegration()) return 1;
  std::printf("PASS: AC-7 barge-in (mechanism + ChatController integration)\n");
  return 0;
}
