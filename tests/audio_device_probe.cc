// 音频设备探针 (手动运行的 M2 烟雾测试，用于 AC-2 链路存活性)。
//
// 枚举设备; 若存在采集设备，用与产品相同的配置 (16k/mono/s16) 打开 AudioCapture
// 持续采集约 500ms，报告读到的样本数与峰值电平。无设备则跳过并返回 0。
//
// 无法在无 GUI/无声卡环境断言「电平随声音跳动」，但能确认: 设备可打开、回调
// 在跑、PCM 正流入环形缓冲。
#include <chrono>
#include <cstdio>
#include <thread>

#include "audio_capture.h"
#include "audio_constants.h"

int main() {
  using namespace std::chrono_literals;
  voice::AudioCapture capture;
  if (!capture.Start()) {
    std::printf("SKIP: no capture device or miniaudio unavailable\n");
    return 0;
  }
  std::printf("capture started (%d Hz / %d ch)\n", voice::kSampleRate,
              voice::kChannels);

  size_t total = 0;
  float peak = 0.0f;
  const auto end = std::chrono::steady_clock::now() + 500ms;
  voice::Pcm16 buf;
  while (std::chrono::steady_clock::now() < end) {
    total += capture.Read(&buf, 4096);
    peak = peak > capture.input_level() ? peak : capture.input_level();
    std::this_thread::sleep_for(10ms);
  }
  capture.Stop();

  std::printf("captured %zu samples (~%.0f ms), peak level %.4f\n", total,
              total * 1000.0 / voice::kSampleRate, peak);
  std::printf("%s: capture pipeline alive\n", total > 0 ? "PASS" : "WARN");
  return 0;
}
