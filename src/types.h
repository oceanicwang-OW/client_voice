// 公共类型定义 (PRD §9.1)。
// 全链路 PCM 统一约定: 16 kHz / 单声道 / 16-bit signed。
#pragma once

#include <cstdint>
#include <vector>

namespace voice {

// 16k mono int16 的 PCM 样本序列。
using Pcm16 = std::vector<int16_t>;

// 单个 Opus 编码帧 (阶段一未启用 Opus 时，承载原始 PCM 字节)。
using OpusPacket = std::vector<uint8_t>;

struct AudioConfig {
  int sample_rate = 16000;
  int channels = 1;
};

}  // namespace voice
