// AC-5 编解码完整性 (PRD §13)。
//
// 对一段语音切 20ms(320) 帧逐帧 Encode -> Decode，校验:
//   - 每帧解码样本数 == 320 (误差 ≤ 1 帧)。
//   - 整段解码样本数 ≈ 编码前 (误差 ≤ 1 帧)。
//   - 启用 Opus 时编码包显著小于原始 PCM (640 字节)，证明确实在压缩。
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio_constants.h"
#include "codec.h"

namespace {
bool Check(bool cond, const char* msg) {
  if (!cond) std::printf("FAIL: %s\n", msg);
  return cond;
}
}  // namespace

int main() {
  constexpr int kFrames = 50;  // 50 * 20ms = 1.0s。
  constexpr int kTotalIn = kFrames * voice::kOpusFrameSamples;

  // 440Hz 正弦，避免 Opus DTX 把静音压成 1 字节包。
  std::vector<int16_t> source(kTotalIn);
  for (int i = 0; i < kTotalIn; ++i) {
    double t = static_cast<double>(i) / voice::kSampleRate;
    source[i] = static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265 * 440.0 * t));
  }

  voice::OpusCodec codec;
  if (!Check(codec.Init(), "codec.Init")) return 1;

  int total_out = 0;
  int max_pkt = 0;
  bool per_frame_ok = true;
  for (int f = 0; f < kFrames; ++f) {
    voice::OpusPacket pkt = codec.Encode(source.data() + f * voice::kOpusFrameSamples);
    if (pkt.empty()) {
      std::printf("FAIL: empty packet at frame %d\n", f);
      return 1;
    }
    max_pkt = static_cast<int>(pkt.size()) > max_pkt ? static_cast<int>(pkt.size()) : max_pkt;
    voice::Pcm16 pcm = codec.Decode(pkt);
    const int diff = std::abs(static_cast<int>(pcm.size()) - voice::kOpusFrameSamples);
    per_frame_ok = per_frame_ok && (diff <= voice::kOpusFrameSamples);  // ≤ 1 帧
    total_out += static_cast<int>(pcm.size());
  }

  if (!Check(per_frame_ok, "per-frame decoded size within 1 frame")) return 1;
  if (!Check(std::abs(total_out - kTotalIn) <= voice::kOpusFrameSamples,
             "total decoded ~= total encoded (<= 1 frame)"))
    return 1;

#if defined(VOICE_ENABLE_OPUS)
  const char* mode = "Opus";
  // 原始 PCM 帧 = 320 * 2 = 640 字节; 24kbps 下 20ms 包应远小于此。
  if (!Check(max_pkt < 640, "Opus packet smaller than raw PCM (compressing)"))
    return 1;
#else
  const char* mode = "PCM-passthrough";
#endif

  std::printf("PASS: AC-5 codec integrity [%s] in=%d out=%d max_pkt=%dB\n", mode,
              kTotalIn, total_out, max_pkt);
  return 0;
}
