#include "chat_controller.h"

#include <chrono>
#include <thread>
#include <vector>

#include "audio_constants.h"

namespace voice {

struct ChatController::Impl {
  std::thread proc_thread;
  std::atomic<bool> proc_running{false};

  // 模块间重新切帧用的累积缓冲 (PRD §15.1)。
  Pcm16 pcm_accum;        // 采集原始累积 -> 切 10ms 给 APM。
  Pcm16 vad_accum;        // APM 处理后累积 -> 切 512 给 VAD。
  Pcm16 utterance;        // 当前一句的处理后 PCM (Utterance Loopback)。
  Pcm16 monitor_accum;    // Realtime Monitor: 直接切 20ms 编码。
  bool collecting = false;  // 仅在 speech 期间累积 utterance，避免空闲无限增长。
};

ChatController::ChatController() : impl_(std::make_unique<Impl>()) {}
ChatController::~ChatController() { Shutdown(); }

bool ChatController::Init() {
  capture_ = std::make_unique<AudioCapture>();
#if defined(VOICE_ENABLE_WEBRTC)
  apm_ = CreateWebrtcApm();       // M6: 真实 AEC/NS/AGC (库缺失时内部回退直通)。
#else
  apm_ = CreatePassthroughApm();  // 阶段一直通。
#endif
  vad_ = std::make_unique<VadDetector>();
  codec_ = std::make_unique<OpusCodec>();
  transport_ = std::make_unique<LoopbackTransport>();
  playback_ = std::make_unique<AudioPlayback>();
  tts_ = CreateTts();

  if (!codec_->Init()) return false;
  if (!tts_->Init()) {
    log_.Push(MessageKind::kLog, "TTS: init 失败, /say 将不可用");
  }
  const bool vad_loaded = vad_->Load("models/silero_vad.onnx");
#if defined(VOICE_ENABLE_SILERO)
  if (!vad_loaded) {
    log_.Push(MessageKind::kLog, "VAD: failed to load Silero model");
    return false;
  }
#else
  (void)vad_loaded;
#endif
  playback_->SetRenderRefSink(apm_.get());

  // 下行: 收到回传音频 -> 解码 -> 入播放队列。
  transport_->on_audio = [this](const OpusPacket& pkt) {
    Pcm16 pcm = codec_->Decode(pkt);
    if (!pcm.empty()) playback_->Enqueue(pcm);
  };
  transport_->on_text = [this](const std::string& text) {
    log_.Push(MessageKind::kEcho, text);
  };
  transport_->on_audio_end = [this]() {
    log_.Push(MessageKind::kLog, "Playback: audio segment received");
  };

  transport_->Start();
  if (!playback_->Start()) {
    log_.Push(MessageKind::kLog, "Playback: failed to start (miniaudio?)");
  }
  ApplySettings();
  return true;
}

void ChatController::Shutdown() {
  OnMicToggle(false);
  if (playback_) playback_->Stop();
  if (transport_) transport_->Stop();
}

void ChatController::OnTextSubmit(const std::string& text) {
  if (text.empty()) return;
  // 文本路径独立于处理线程: 这里实时下发 Loopback 延迟，使滑块在闭麦下也生效
  // (SetDelayMs 为原子操作，跨线程安全)。AC-9。
  transport_->SetDelayMs(settings_.loopback_delay_ms.load());
  log_.Push(MessageKind::kMe, text);
  transport_->SendText(text);
}

void ChatController::OnSpeak(const std::string& text) {
  if (text.empty()) return;
  // 与文本路径一致: 实时下发 Loopback 延迟 (AC-9)。
  transport_->SetDelayMs(settings_.loopback_delay_ms.load());
  log_.Push(MessageKind::kMe, "/say " + text);

  Pcm16 pcm = tts_->Synthesize(text);  // UTF-8 文本 -> 16k/mono/int16。
  if (pcm.empty()) {
    log_.Push(MessageKind::kLog,
              "TTS: 合成失败或未启用 (需 -DVOICE_ENABLE_TTS=ON 且系统有语音)");
    return;
  }

  // 补零到 Opus 帧长整数倍, 确保整段都被 EmitFrames 发出 (无残余留缓冲)。
  const size_t rem = pcm.size() % kOpusFrameSamples;
  if (rem != 0) pcm.resize(pcm.size() + (kOpusFrameSamples - rem), 0);

  // EmitFrames 会消费(erase) buffer, 故先算时长。
  const int ms = static_cast<int>(pcm.size() * 1000 / kSampleRate);
  EmitFrames(pcm);  // 切 20ms -> Opus 编码 -> 下行 Loopback 回传 -> 播放。
  log_.Push(MessageKind::kVoice,
            std::to_string(ms / 1000.0).substr(0, 4) + "s TTS played");
}

void ChatController::OnMicToggle(bool on) {
  if (on == ui_state_.mic_on.load()) return;
  if (on) {
    if (!capture_->Start()) {
      log_.Push(MessageKind::kLog, "Capture: failed to start (miniaudio?)");
      return;
    }
    ui_state_.mic_on.store(true);
    impl_->proc_running.store(true);
    impl_->proc_thread = std::thread([this] { ProcessLoop(); });
    log_.Push(MessageKind::kLog, "Mic: ON (continuous listening)");
  } else {
    impl_->proc_running.store(false);
    if (impl_->proc_thread.joinable()) impl_->proc_thread.join();
    if (capture_) capture_->Stop();
    // 闭麦清空上行缓冲与状态 (PRD §5.2)。
    impl_->pcm_accum.clear();
    impl_->vad_accum.clear();
    impl_->utterance.clear();
    impl_->monitor_accum.clear();
    impl_->collecting = false;
    if (vad_) vad_->Reset();
    ui_state_.mic_on.store(false);
    ui_state_.speaking.store(false);
    log_.Push(MessageKind::kLog, "Mic: OFF");
  }
}

void ChatController::ApplySettings() {
  vad_->SetThreshold(settings_.vad_threshold.load());
  vad_->SetMinSilenceMs(settings_.min_silence_ms.load());
  apm_->SetAecEnabled(settings_.aec_enabled.load());
  apm_->SetNsEnabled(settings_.ns_enabled.load());
  transport_->SetDelayMs(settings_.loopback_delay_ms.load());
}

void ChatController::ProcessLoop() {
  Pcm16 chunk;
  while (impl_->proc_running.load()) {
    ApplySettings();
    capture_->Read(&chunk, 1024);
    ui_state_.input_level.store(capture_->input_level());
    if (chunk.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    ProcessSamples(chunk.data(), chunk.size());
  }
}

void ChatController::FeedAudioForTest(const Pcm16& pcm) {
  // 测试入口: 用提供的 PCM 当作麦克风输入，跑与处理线程完全相同的链路，
  // 无需真实声卡。调用方需先 Init()。
  ApplySettings();
  ProcessSamples(pcm.data(), pcm.size());
}

void ChatController::PumpPlaybackForTest(size_t frames) {
  Pcm16 scratch(frames);
  playback_->fill_output_internal(scratch.data(), frames);
  ui_state_.playing.store(playback_->IsPlaying());
}

void ChatController::ProcessSamples(const int16_t* data, size_t count) {
  impl_->pcm_accum.insert(impl_->pcm_accum.end(), data, data + count);

  // 1) 切 10ms(160) 给 APM, 处理后进入下游累积。
  size_t off = 0;
  while (impl_->pcm_accum.size() - off >= kApmFrameSamples) {
    int16_t* frame = impl_->pcm_accum.data() + off;
    apm_->ProcessCapture(frame);
    off += kApmFrameSamples;
    if (settings_.realtime_monitor.load()) {
      impl_->monitor_accum.insert(impl_->monitor_accum.end(),
                                  frame, frame + kApmFrameSamples);
    } else {
      impl_->vad_accum.insert(impl_->vad_accum.end(),
                              frame, frame + kApmFrameSamples);
      if (impl_->collecting) {
        impl_->utterance.insert(impl_->utterance.end(),
                                frame, frame + kApmFrameSamples);
      }
    }
  }
  impl_->pcm_accum.erase(impl_->pcm_accum.begin(),
                         impl_->pcm_accum.begin() + off);

  if (settings_.realtime_monitor.load()) {
    EmitFrames(impl_->monitor_accum);  // §7.2 不经 VAD，直接回环。
    ui_state_.playing.store(playback_->IsPlaying());
    return;
  }

  // 2) 切 512(32ms) 给 VAD 端点检测。
  size_t voff = 0;
  while (impl_->vad_accum.size() - voff >= kVadWindowSamples) {
    VadEvent ev = vad_->Push(impl_->vad_accum.data() + voff);
    voff += kVadWindowSamples;
    ui_state_.vad_prob.store(vad_->last_prob());
    HandleVadEvent(ev);
  }
  impl_->vad_accum.erase(impl_->vad_accum.begin(),
                         impl_->vad_accum.begin() + voff);
  ui_state_.playing.store(playback_->IsPlaying());
}

void ChatController::HandleVadEvent(VadEvent ev) {
  switch (ev) {
    case VadEvent::kSpeechStart:
      ui_state_.speaking.store(true);
      log_.Push(MessageKind::kLog, "VAD: speech start");
      // Barge-in: 播放中开口立即打断 (PRD §FR-8 / §15.4)。
      if (playback_->IsPlaying()) {
        playback_->Flush();
        transport_->CancelPendingAudio();
        log_.Push(MessageKind::kLog, "barge-in: playback flushed");
      }
      // 新一句开始: 清空旧累积，开始收集本句。
      impl_->utterance.clear();
      impl_->collecting = true;
      break;
    case VadEvent::kSpeechEnd: {
      ui_state_.speaking.store(false);
      impl_->collecting = false;
      const int ms = static_cast<int>(impl_->utterance.size() * 1000 /
                                      kSampleRate);
      if (ms < kMinUtteranceMs) {  // 误触发，丢弃。
        impl_->utterance.clear();
        log_.Push(MessageKind::kLog, "VAD: discarded short utterance");
        break;
      }
      log_.Push(MessageKind::kLog,
                "VAD: speech end (" + std::to_string(ms / 1000.0).substr(0, 4) +
                    "s)");
      EmitFrames(impl_->utterance);
      log_.Push(MessageKind::kVoice,
                std::to_string(ms / 1000.0).substr(0, 4) +
                    "s utterance looped back");
      impl_->utterance.clear();
      break;
    }
    case VadEvent::kNone:
      break;
  }
}

void ChatController::EmitFrames(Pcm16& buffer) {
  // 切 20ms(320) Opus 编码 -> 发送; 余数留待下次。
  size_t off = 0;
  while (buffer.size() - off >= kOpusFrameSamples) {
    OpusPacket pkt = codec_->Encode(buffer.data() + off);
    off += kOpusFrameSamples;
    if (!pkt.empty()) transport_->SendAudio(pkt);
  }
  buffer.erase(buffer.begin(), buffer.begin() + off);
}

}  // namespace voice
