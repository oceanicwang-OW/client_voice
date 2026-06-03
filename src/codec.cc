#include "codec.h"

#include <cstring>

#if defined(VOICE_ENABLE_OPUS)
#include <opus.h>
#endif

namespace voice {

struct OpusCodec::Impl {
  int sample_rate = kSampleRate;
  int channels = kChannels;
  int bitrate = kOpusBitrate;
#if defined(VOICE_ENABLE_OPUS)
  OpusEncoder* enc = nullptr;
  OpusDecoder* dec = nullptr;
  ~Impl() {
    if (enc) opus_encoder_destroy(enc);
    if (dec) opus_decoder_destroy(dec);
  }
#endif
};

OpusCodec::OpusCodec() : impl_(std::make_unique<Impl>()) {}
OpusCodec::~OpusCodec() = default;

bool OpusCodec::Init(int sample_rate, int channels, int bitrate) {
  impl_->sample_rate = sample_rate;
  impl_->channels = channels;
  impl_->bitrate = bitrate;
#if defined(VOICE_ENABLE_OPUS)
  int err = 0;
  impl_->enc = opus_encoder_create(sample_rate, channels,
                                   OPUS_APPLICATION_VOIP, &err);
  if (err != OPUS_OK || !impl_->enc) return false;
  opus_encoder_ctl(impl_->enc, OPUS_SET_BITRATE(bitrate));
  impl_->dec = opus_decoder_create(sample_rate, channels, &err);
  return err == OPUS_OK && impl_->dec != nullptr;
#else
  return true;  // PCM 直通模式无需初始化。
#endif
}

OpusPacket OpusCodec::Encode(const int16_t* frame_320) {
#if defined(VOICE_ENABLE_OPUS)
  OpusPacket out(4000);  // 上限缓冲。
  const int n = opus_encode(impl_->enc, frame_320, kOpusFrameSamples,
                            out.data(), static_cast<int>(out.size()));
  if (n < 0) return {};
  out.resize(static_cast<size_t>(n));
  return out;
#else
  // PCM 直通: 把 320 个 int16 样本按小端字节装入包 (PRD §11)。
  OpusPacket out(kOpusFrameSamples * sizeof(int16_t));
  std::memcpy(out.data(), frame_320, out.size());
  return out;
#endif
}

Pcm16 OpusCodec::Decode(const OpusPacket& pkt) {
#if defined(VOICE_ENABLE_OPUS)
  Pcm16 out(kOpusFrameSamples);
  const int n = opus_decode(impl_->dec, pkt.data(),
                            static_cast<int>(pkt.size()), out.data(),
                            kOpusFrameSamples, 0);
  if (n < 0) return {};
  out.resize(static_cast<size_t>(n));
  return out;
#else
  Pcm16 out(pkt.size() / sizeof(int16_t));
  std::memcpy(out.data(), pkt.data(), out.size() * sizeof(int16_t));
  return out;
#endif
}

}  // namespace voice
