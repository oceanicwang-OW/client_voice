// 文字转语音 (TTS) —— 把文本合成为 16k/mono/int16 PCM, 喂入下行音频链路。
//
// 接口可插拔 (遵循项目"接口 + stub"惯例):
//   - VOICE_ENABLE_TTS 且 Windows: Sapi5Tts (系统 SAPI5, 无额外依赖/无模型)。
//   - 否则: NullTts (Synthesize 返回空, 由调用方记日志)。
#pragma once

#include <memory>
#include <string>

#include "types.h"

namespace voice {

class ITextToSpeech {
 public:
  virtual ~ITextToSpeech() = default;

  // 初始化引擎。失败返回 false (调用方仍可继续, Synthesize 将返回空)。
  virtual bool Init() = 0;

  // UTF-8 文本 -> 16k/mono/int16 PCM; 失败或不支持返回空 Pcm16。
  virtual Pcm16 Synthesize(const std::string& utf8_text) = 0;
};

std::unique_ptr<ITextToSpeech> CreateTts();

}  // namespace voice
