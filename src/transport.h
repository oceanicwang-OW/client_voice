// 传输层 (PRD §9.6) —— Loopback 替代 WebSocket + AIServer。
//
// 真实接入服务端时，只需新增 WebSocketTransport 实现同一 ITransport 接口
// 替换即可，其余代码不动。
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "types.h"

namespace voice {

class ITransport {
 public:
  virtual ~ITransport() = default;

  virtual void SendAudio(const OpusPacket& pkt) = 0;
  virtual void SendText(const std::string& text) = 0;

  // barge-in: 取消尚未回传的音频 (本测试中清空 Loopback 待回传队列)。
  virtual void CancelPendingAudio() = 0;

  // 回调: 收到 (回传的) 音频帧 / 文本 / 一段音频结束信号。
  std::function<void(const OpusPacket&)> on_audio;
  std::function<void(const std::string&)> on_text;
  std::function<void()> on_audio_end;
};

// LoopbackTransport: 把 Send* 的内容在设定延迟后通过回调原样回传
// (文本可加 "Echo: " 前缀)，用延迟队列在独立线程实现。
class LoopbackTransport : public ITransport {
 public:
  LoopbackTransport();
  ~LoopbackTransport() override;

  void SendAudio(const OpusPacket& pkt) override;
  void SendText(const std::string& text) override;
  void CancelPendingAudio() override;

  void SetDelayMs(int ms);  // 模拟网络/服务端处理延迟 (0~1000)。

  void Start();
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace voice
