#include "transport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>

namespace voice {

namespace {
using Clock = std::chrono::steady_clock;

struct Item {
  Clock::time_point due;             // 到期回传时间。
  std::variant<OpusPacket, std::string> payload;
  bool is_audio;
};
}  // namespace

struct LoopbackTransport::Impl {
  std::thread worker;
  std::mutex mu;
  std::condition_variable cv;
  std::deque<Item> queue;
  std::atomic<bool> running{false};
  std::atomic<int> delay_ms{300};

  void Run(LoopbackTransport* owner);
};

void LoopbackTransport::Impl::Run(LoopbackTransport* owner) {
  std::unique_lock<std::mutex> lk(mu);
  while (running.load()) {
    if (queue.empty()) {
      cv.wait(lk);
      continue;
    }
    const auto due = queue.front().due;
    if (Clock::now() < due) {
      cv.wait_until(lk, due);
      continue;
    }
    Item item = std::move(queue.front());
    queue.pop_front();
    lk.unlock();
    // 回调在锁外执行，避免回调里再次 Send* 造成死锁。
    if (item.is_audio) {
      if (owner->on_audio) owner->on_audio(std::get<OpusPacket>(item.payload));
    } else {
      if (owner->on_text) owner->on_text(std::get<std::string>(item.payload));
    }
    lk.lock();
    // 该来源的音频若已全部回传，发一次结束信号 (简化: 队列再无音频项时)。
    if (item.is_audio) {
      bool more_audio = false;
      for (const auto& it : queue) more_audio = more_audio || it.is_audio;
      if (!more_audio && owner->on_audio_end) {
        lk.unlock();
        owner->on_audio_end();
        lk.lock();
      }
    }
  }
}

LoopbackTransport::LoopbackTransport() : impl_(std::make_unique<Impl>()) {}

LoopbackTransport::~LoopbackTransport() { Stop(); }

void LoopbackTransport::Start() {
  if (impl_->running.exchange(true)) return;
  impl_->worker = std::thread([this] { impl_->Run(this); });
}

void LoopbackTransport::Stop() {
  if (!impl_->running.exchange(false)) return;
  impl_->cv.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
}

void LoopbackTransport::SetDelayMs(int ms) {
  if (ms < 0) ms = 0;
  impl_->delay_ms.store(ms);
}

void LoopbackTransport::SendAudio(const OpusPacket& pkt) {
  const auto due = Clock::now() + std::chrono::milliseconds(impl_->delay_ms.load());
  {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->queue.push_back({due, pkt, /*is_audio=*/true});
  }
  impl_->cv.notify_all();
}

void LoopbackTransport::SendText(const std::string& text) {
  // 原样回环 (faithful loopback)。"Echo:" 标签是显示层职责 (MessageKind::kEcho)，
  // 这样真实 WebSocketTransport 替换时无需改动文本内容 (PRD §9.6)。
  const auto due = Clock::now() + std::chrono::milliseconds(impl_->delay_ms.load());
  {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->queue.push_back({due, text, /*is_audio=*/false});
  }
  impl_->cv.notify_all();
}

void LoopbackTransport::CancelPendingAudio() {
  // barge-in: 丢弃尚未回传的音频项，保留文本。
  std::lock_guard<std::mutex> lk(impl_->mu);
  for (auto it = impl_->queue.begin(); it != impl_->queue.end();) {
    it = it->is_audio ? impl_->queue.erase(it) : std::next(it);
  }
}

}  // namespace voice
