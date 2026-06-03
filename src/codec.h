// 音频编解码 (PRD §9.5) —— 基于 libopus。
//
// 阶段一未启用 Opus 时 (VOICE_ENABLE_OPUS=OFF)，Encode/Decode 退化为 PCM
// 直通: 把 int16 样本按字节装入 OpusPacket，解码时还原 (PRD §11)。
#pragma once

#include <memory>

#include "audio_constants.h"
#include "types.h"

namespace voice {

class OpusCodec {
 public:
  OpusCodec();
  ~OpusCodec();

  OpusCodec(const OpusCodec&) = delete;
  OpusCodec& operator=(const OpusCodec&) = delete;

  bool Init(int sample_rate = kSampleRate, int channels = kChannels,
            int bitrate = kOpusBitrate);

  // 输入 320 samples(20ms)，输出一个 Opus 包。
  OpusPacket Encode(const int16_t* frame_320);
  // 输入一个 Opus 包，输出 320 samples。
  Pcm16 Decode(const OpusPacket& pkt);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace voice
