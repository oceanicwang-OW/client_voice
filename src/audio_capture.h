// 麦克风采集 (PRD §9.2) —— 基于 miniaudio。
//
// 采集回调线程只做: 重采样到 16k/mono + 写入 SPSC 环形缓冲，绝不阻塞、
// 不分配大内存、不打日志 (PRD §10)。处理线程通过 Read() 取连续 PCM。
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include "ring_buffer.h"
#include "types.h"

namespace voice {

class AudioCapture {
 public:
  AudioCapture();
  ~AudioCapture();

  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  // 打开默认输入设备，启动持续采集 (非按住录音)。成功返回 true。
  bool Start();
  void Stop();

  // 处理线程调用: 取出最多 max_samples 个 16k/mono 样本，返回实际读取数。
  size_t Read(Pcm16* out, size_t max_samples);

  bool IsRunning() const { return running_.load(); }

  // 当前输入电平 (RMS, 归一化到 0~1)，供 UI 电平条显示。
  float input_level() const { return input_level_.load(); }

  // 仅供采集回调 (实时线程) 使用的内部桥接，勿在业务代码调用。
  void set_input_level_internal(float level);
  void ring_write_internal(const int16_t* data, size_t count);

 private:
  struct Impl;                  // miniaudio 设备等实现细节 (PIMPL)。
  std::unique_ptr<Impl> impl_;
  std::atomic<bool> running_{false};
  std::atomic<float> input_level_{0.0f};
};

}  // namespace voice
