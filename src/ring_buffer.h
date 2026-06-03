// 单生产者单消费者 (SPSC) 无锁环形缓冲 (PRD §10)。
//
// 用于采集回调线程 -> 处理线程、处理线程 -> 播放回调线程之间传递 PCM 样本。
// 仅在“恰好一个生产者线程 + 恰好一个消费者线程”下保证无锁安全。
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace voice {

template <typename T>
class SpscRingBuffer {
 public:
  // capacity: 可存储元素的容量 (内部多分配 1 槽用于区分空/满)。
  explicit SpscRingBuffer(size_t capacity)
      : buffer_(capacity + 1), capacity_(capacity + 1) {}

  // 生产者线程调用: 写入最多 count 个元素，返回实际写入数。
  size_t Write(const T* data, size_t count) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    const size_t free_slots = FreeSlots(head, tail);
    const size_t n = count < free_slots ? count : free_slots;
    for (size_t i = 0; i < n; ++i) {
      buffer_[(head + i) % capacity_] = data[i];
    }
    head_.store((head + n) % capacity_, std::memory_order_release);
    return n;
  }

  // 消费者线程调用: 读取最多 max_count 个元素到 out，返回实际读取数。
  size_t Read(T* out, size_t max_count) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t available = Available(head, tail);
    const size_t n = max_count < available ? max_count : available;
    for (size_t i = 0; i < n; ++i) {
      out[i] = buffer_[(tail + i) % capacity_];
    }
    tail_.store((tail + n) % capacity_, std::memory_order_release);
    return n;
  }

  // 当前可读元素数 (近似，跨线程仅供观测)。
  size_t Size() const {
    return Available(head_.load(std::memory_order_acquire),
                     tail_.load(std::memory_order_acquire));
  }

  // 清空 (仅在无并发访问时调用，例如 barge-in 复位前已停采集/播放)。
  void Clear() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

 private:
  size_t Available(size_t head, size_t tail) const {
    return (head + capacity_ - tail) % capacity_;
  }
  size_t FreeSlots(size_t head, size_t tail) const {
    return capacity_ - 1 - Available(head, tail);
  }

  std::vector<T> buffer_;
  const size_t capacity_;
  std::atomic<size_t> head_{0};  // 生产者写位置。
  std::atomic<size_t> tail_{0};  // 消费者读位置。
};

}  // namespace voice
