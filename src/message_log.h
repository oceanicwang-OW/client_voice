// 消息/日志总线 (PRD §5.1 / §10)。
//
// 处理线程、Loopback 线程等向 UI 消息区投递文本/日志: 用一个加锁小队列
// (非实时路径)。UI/主线程每帧 Drain 取出渲染。绝不在采集/播放回调里调用。
#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace voice {

enum class MessageKind {
  kMe,      // "Me: ..."  用户发出的文本
  kEcho,    // "Echo: ..." Loopback 回显文本
  kVoice,   // "Voice: x.xs looped back" 语音闭环事件
  kLog,     // 系统日志 (VAD/播放/barge-in 等)
};

struct LogMessage {
  MessageKind kind;
  std::string text;
};

// 线程安全的 SPMC/MPSC 日志队列 (生产者: 各工作线程; 消费者: UI 线程)。
class MessageLog {
 public:
  void Push(MessageKind kind, std::string text) {
    std::lock_guard<std::mutex> lk(mu_);
    pending_.push_back({kind, std::move(text)});
  }

  // UI 线程每帧调用: 取走全部待处理消息 (清空内部队列)。
  std::vector<LogMessage> Drain() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<LogMessage> out;
    out.swap(pending_);
    return out;
  }

 private:
  std::mutex mu_;
  std::vector<LogMessage> pending_;
};

}  // namespace voice
