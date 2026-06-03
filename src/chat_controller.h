// 控制器 (PRD §9.8) —— 串起上行/下行链路与文本路径。
//
// 内部驱动处理线程:
//   Read 采集 -> APM(10ms) -> VAD(512) -> 攒句 -> kSpeechEnd 时切 20ms 编码
//   -> transport.SendAudio
//   transport.on_audio -> Decode -> playback.Enqueue
//   VAD kSpeechStart 且 playback.IsPlaying() -> playback.Flush() (barge-in)
#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "audio_capture.h"
#include "audio_playback.h"
#include "audio_processor.h"
#include "codec.h"
#include "message_log.h"
#include "transport.h"
#include "vad.h"

namespace voice {

// UI 可观测的共享运行状态 (全部原子，UI 线程只读)。
struct UiState {
  std::atomic<bool> mic_on{false};
  std::atomic<bool> speaking{false};   // VAD: 说话中
  std::atomic<bool> playing{false};    // 播放中
  std::atomic<float> input_level{0.0f};
  std::atomic<float> vad_prob{0.0f};
};

// 可由 UI 实时调整的参数 (PRD §5.1 设置区)。
struct Settings {
  std::atomic<float> vad_threshold{0.5f};
  std::atomic<int> min_silence_ms{600};
  std::atomic<bool> aec_enabled{true};
  std::atomic<bool> ns_enabled{true};
  std::atomic<int> loopback_delay_ms{300};
  std::atomic<bool> realtime_monitor{false};  // false=Utterance Loopback
};

class ChatController {
 public:
  ChatController();
  ~ChatController();

  ChatController(const ChatController&) = delete;
  ChatController& operator=(const ChatController&) = delete;

  bool Init();   // 创建并初始化各模块、启动 transport，但不开麦。
  void Shutdown();

  void OnTextSubmit(const std::string& text);  // 文本路径。
  void OnMicToggle(bool on);                    // 开/闭麦。

  // 测试入口: 把一段 16k/mono PCM 当作麦克风输入跑完整处理链路 (无需声卡)。
  void FeedAudioForTest(const Pcm16& pcm);
  // 测试入口: 模拟播放设备回调拉取 frames 个样本 (无需声卡), 用于使播放进入
  // 播放态以验证 barge-in。
  void PumpPlaybackForTest(size_t frames);

  // UI 线程访问。
  MessageLog& log() { return log_; }
  UiState& ui_state() { return ui_state_; }
  Settings& settings() { return settings_; }

 private:
  void ProcessLoop();              // 处理线程主循环。
  void ProcessSamples(const int16_t* data, size_t count);  // 单块处理 (共享)。
  void ApplySettings();            // 把 Settings 下发到各模块。
  void HandleVadEvent(VadEvent ev);  // 处理 VAD 起止 (含 barge-in)。
  void EmitFrames(Pcm16& buffer);    // 切 20ms 编码并经 transport 发送。

  std::unique_ptr<AudioCapture> capture_;
  std::unique_ptr<IAudioProcessor> apm_;
  std::unique_ptr<VadDetector> vad_;
  std::unique_ptr<OpusCodec> codec_;
  std::unique_ptr<LoopbackTransport> transport_;
  std::unique_ptr<AudioPlayback> playback_;

  MessageLog log_;
  UiState ui_state_;
  Settings settings_;

  struct Impl;             // 处理线程/缓冲等实现细节。
  std::unique_ptr<Impl> impl_;
};

}  // namespace voice
