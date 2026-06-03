// 麦克风电平实时监视 (手动诊断工具, 非自动化测试)。
//
// 打开默认采集设备 (与产品同配置 16k/mono/s16), 每 ~150ms 打印一次 RMS 电平
// 与峰值, 持续约 15 秒。用于确认: 麦克风是否有信号、说话时电平是否跳动。
//   - 说话时数字明显跳动 -> 麦克风正常, /mic 不回传应查 VAD 阈值/断句。
//   - 全程接近 0 -> 默认录音设备无信号 (静音/选错设备/无麦), 属系统设置问题。
#include <chrono>
#include <cstdio>
#include <thread>

#include "audio_capture.h"
#include "audio_constants.h"

int main() {
  using namespace std::chrono_literals;
  voice::AudioCapture capture;
  if (!capture.Start()) {
    std::printf("SKIP: 无采集设备或 miniaudio 不可用\n");
    return 0;
  }
  std::printf("采集已启动 (%d Hz / %d ch)。请对着麦克风说话, 观察电平...\n",
              voice::kSampleRate, voice::kChannels);
  std::printf("(约 15 秒后自动结束)\n\n");

  voice::Pcm16 buf;
  float session_peak = 0.0f;
  const auto end = std::chrono::steady_clock::now() + 15s;
  while (std::chrono::steady_clock::now() < end) {
    // 排空环形缓冲, 取这一小段的最大 RMS 作为瞬时电平。
    float level = 0.0f;
    for (int i = 0; i < 10; ++i) {
      capture.Read(&buf, 4096);
      const float l = capture.input_level();
      if (l > level) level = l;
      std::this_thread::sleep_for(15ms);
    }
    if (level > session_peak) session_peak = level;

    // 文本电平条 (0.0 ~ 0.3 满刻度)。
    const int bars = static_cast<int>(level / 0.30f * 40.0f);
    char meter[41];
    for (int i = 0; i < 40; ++i) meter[i] = (i < bars) ? '#' : '.';
    meter[40] = '\0';
    std::printf("\r电平 %.4f |%s|", level, meter);
    std::fflush(stdout);
  }
  capture.Stop();

  std::printf("\n\n本次最大电平: %.4f\n", session_peak);
  if (session_peak < 0.005f) {
    std::printf("结论: 几乎无信号 -> 请检查 Windows 录音设备 "
                "(默认设备/静音/麦克风音量)。\n");
  } else {
    std::printf("结论: 麦克风有信号 -> 若 /mic 仍不回传, 多为 VAD 阈值或断句"
                "停顿不足, 可继续调。\n");
  }
  return 0;
}
