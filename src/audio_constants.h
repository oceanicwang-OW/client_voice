// 关键音频参数 (PRD §8) —— 实现必须遵守，集中定义避免散落魔数。
//
// 各模块帧长不同 (采集任意 -> APM 10ms -> VAD 512 -> Opus 20ms)，模块间
// 必须用环形缓冲重新切帧对齐 (PRD §8 注 / §15.1)。
#pragma once

namespace voice {

inline constexpr int kSampleRate = 16000;  // 全链路统一采样率 (Hz)。
inline constexpr int kChannels = 1;        // 单声道。

// 帧长 (单位: 样本数 @16k)。
inline constexpr int kApmFrameSamples = 160;   // WebRTC APM 固定 10 ms。
inline constexpr int kVadWindowSamples = 512;   // Silero VAD v4/v5: 32 ms。
inline constexpr int kOpusFrameSamples = 320;   // Opus 20 ms。

// 时长换算。
inline constexpr int kApmFrameMs = 10;
inline constexpr int kOpusFrameMs = 20;

// VAD 端点检测默认参数。
inline constexpr float kVadDefaultThreshold = 0.5f;  // 概率 > 阈值视为有声。
inline constexpr int kVadSpeechStartWindows = 2;     // 连续有声窗确认句首。
inline constexpr int kVadDefaultMinSilenceMs = 600;  // 句末静音判定 (可调)。
inline constexpr int kVadMinSilenceMsRange[2] = {200, 1500};
inline constexpr int kMinUtteranceMs = 300;          // 短于此丢弃 (误触发)。

// Opus 编码参数。
inline constexpr int kOpusBitrate = 24000;  // bps，可调。

// Jitter buffer 预缓冲目标 (ms)，攒够再起播。
inline constexpr int kJitterPrebufferMsMin = 60;
inline constexpr int kJitterPrebufferMsMax = 120;

// Loopback 模拟延迟默认值 (ms)。
inline constexpr int kLoopbackDefaultDelayMs = 300;

}  // namespace voice
