// 播放与缓冲 (PRD §9.7) —— jitter buffer + miniaudio 播放。
//
// 播放回调线程只从队列取数据填充输出，不足时填静音；攒够预缓冲
// (目标 60~120ms) 再起播。每填一帧 10ms，同步喂给 IAudioProcessor::
// ProcessRenderRef 作 AEC 参考 (PRD §15.2)。
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include "audio_processor.h"
#include "types.h"

namespace voice {

class AudioPlayback {
 public:
  AudioPlayback();
  ~AudioPlayback();

  AudioPlayback(const AudioPlayback&) = delete;
  AudioPlayback& operator=(const AudioPlayback&) = delete;

  bool Start();
  void Stop();

  void Enqueue(const Pcm16& pcm);  // 解码后的 PCM 入队。
  void Flush();                    // barge-in: 清空并停止当前播放。

  bool IsPlaying() const { return playing_.load(); }

  // 设置 AEC 参考接收方: 播放每帧 10ms 同步喂给它 (可为 nullptr)。
  void SetRenderRefSink(IAudioProcessor* apm);

  // 仅供播放回调 (实时线程) 使用的内部桥接，勿在业务代码调用。
  void fill_output_internal(int16_t* output, size_t frame_count);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<bool> playing_{false};
};

}  // namespace voice
