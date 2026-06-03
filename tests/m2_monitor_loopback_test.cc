// M2 §7.2 Realtime Monitor 数据通路验收 (无音频硬件)。
//
// 用合成 PCM 替代麦克风，跑完真实的下行链路:
//   ramp PCM -> OpusCodec(PCM 直通) -> LoopbackTransport -> on_audio
//             -> Decode -> AudioPlayback jitter buffer -> fill_output_internal
// 验证:
//   1) 样本完整、有序地往返 (编解码 + 回环不损坏数据) —— AC-6 基础。
//   2) jitter buffer 攒够预缓冲前输出静音、之后顺序播出。
//   3) 播放每 10ms(160) 同步喂一帧给 AEC render 参考 —— PRD §15.2。
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "audio_constants.h"
#include "audio_playback.h"
#include "codec.h"
#include "transport.h"

namespace {

// 统计 render 参考帧被喂入的次数。
class CountingApm : public voice::IAudioProcessor {
 public:
  void ProcessCapture(int16_t*) override {}
  void ProcessRenderRef(const int16_t*) override { ++count; }
  void SetAecEnabled(bool) override {}
  void SetNsEnabled(bool) override {}
  std::atomic<int> count{0};
};

bool Check(bool cond, const char* msg) {
  if (!cond) std::printf("FAIL: %s\n", msg);
  return cond;
}

}  // namespace

int main() {
  using namespace std::chrono_literals;
  constexpr int kFrames = 20;  // 20 * 320 = 6400 样本 (> 预缓冲 1920)。
  constexpr int kTotal = kFrames * voice::kOpusFrameSamples;

  // 合成 300Hz 正弦 (足够响，便于有损编码下也非静音)。
  std::vector<int16_t> source(kTotal);
  for (int i = 0; i < kTotal; ++i) {
    double t = static_cast<double>(i) / voice::kSampleRate;
    source[i] = static_cast<int16_t>(8000.0 * std::sin(2.0 * 3.14159265 * 300.0 * t));
  }

  voice::OpusCodec codec;
  if (!Check(codec.Init(), "codec.Init")) return 1;

  voice::AudioPlayback playback;  // 不 Start(): 直接驱动 fill，无需设备。
  CountingApm apm;
  playback.SetRenderRefSink(&apm);

  // 预缓冲检查: 入队前拉取应得静音且未在播放。
  {
    std::vector<int16_t> out(160, 1234);
    playback.fill_output_internal(out.data(), out.size());
    bool all_zero = true;
    for (int16_t s : out) all_zero = all_zero && (s == 0);
    if (!Check(all_zero && !playback.IsPlaying(),
               "prebuffer: silence before enough queued"))
      return 1;
  }

  std::atomic<int> got_packets{0};
  voice::LoopbackTransport transport;
  transport.SetDelayMs(0);
  transport.on_audio = [&](const voice::OpusPacket& pkt) {
    playback.Enqueue(codec.Decode(pkt));
    ++got_packets;
  };
  transport.Start();

  // 上行: 切 20ms 帧编码并发送 (模拟 monitor 模式持续回环)。
  for (int f = 0; f < kFrames; ++f) {
    voice::OpusPacket pkt = codec.Encode(source.data() + f * voice::kOpusFrameSamples);
    if (!Check(!pkt.empty(), "encode produced packet")) return 1;
    transport.SendAudio(pkt);
  }

  // 等待全部回传。
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (got_packets.load() < kFrames &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  transport.Stop();
  if (!Check(got_packets.load() == kFrames, "all packets looped back")) return 1;

  // 下行: 分块拉取播放输出，校验与源信号逐样本一致、顺序无误。
  std::vector<int16_t> played;
  played.reserve(kTotal);
  for (int f = 0; f < kFrames; ++f) {
    std::vector<int16_t> out(voice::kOpusFrameSamples);
    playback.fill_output_internal(out.data(), out.size());
    played.insert(played.end(), out.begin(), out.end());
  }
  if (!Check(static_cast<int>(played.size()) == kTotal,
             "played length matches input")) return 1;
#if defined(VOICE_ENABLE_OPUS)
  // Opus 有损: 不能逐样本比对; 校验通路确实搬运了音频 (输出非静音)。
  bool any_nonzero = false;
  for (int16_t s : played) any_nonzero = any_nonzero || (s != 0);
  if (!Check(any_nonzero, "played output non-silent (lossy codec path)")) return 1;
#else
  // PCM 直通无损: 逐样本完全一致、顺序无误。
  bool exact = true;
  for (int i = 0; i < kTotal; ++i) exact = exact && (played[i] == source[i]);
  if (!Check(exact, "played samples match source (intact + ordered)")) return 1;
#endif

  // render 参考: 每 160 样本一帧 -> kTotal/160 次。
  const int expected_ref = kTotal / voice::kApmFrameSamples;
  if (!Check(apm.count.load() == expected_ref, "render-ref fed per 10ms frame"))
    return 1;

  std::printf("PASS: M2 monitor loopback (%d samples round-tripped, %d ref frames)\n",
              kTotal, apm.count.load());
  return 0;
}
