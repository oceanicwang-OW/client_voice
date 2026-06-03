// 音频处理 AEC/NS/AGC (PRD §9.3) —— 基于 WebRTC APM；阶段一用直通 stub。
//
// AEC 需要“远端参考信号”= 即将从扬声器播放的音频。播放链路每填一帧 10ms，
// 必须同步喂给 ProcessRenderRef，且与采集帧按 10ms 帧时间对齐 (PRD §15.2)。
#pragma once

#include <cstdint>
#include <memory>

namespace voice {

class IAudioProcessor {
 public:
  virtual ~IAudioProcessor() = default;

  // 处理一帧 10ms (160 samples) 近端采集音频，原地修改。
  virtual void ProcessCapture(int16_t* frame_160) = 0;
  // 提供远端 (将要播放) 的一帧 10ms，供 AEC 做参考。
  virtual void ProcessRenderRef(const int16_t* frame_160) = 0;

  virtual void SetAecEnabled(bool enabled) = 0;
  virtual void SetNsEnabled(bool enabled) = 0;
};

// 工厂。阶段一可用 passthrough 先跑通链路 (PRD §11)。
std::unique_ptr<IAudioProcessor> CreatePassthroughApm();
std::unique_ptr<IAudioProcessor> CreateWebrtcApm();

}  // namespace voice
