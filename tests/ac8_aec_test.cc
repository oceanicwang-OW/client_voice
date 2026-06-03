// AC-8 AEC (PRD §13)。
//
// 始终运行: 经工厂创建 APM，按 10ms(160) 帧喂 capture/render 参考，切换 AEC/NS
//   开关，验证不崩溃 (直通构建下即为 plumbing 烟雾测试)。
// 仅 VOICE_ENABLE_WEBRTC: 构造"扬声器回授"场景——near-end = 0.8*far-end(纯回声)，
//   经收敛后断言输出能量显著低于输入 (AEC 消除回授, 即"外放不自激")。
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "audio_constants.h"
#include "audio_processor.h"

namespace {
double FrameRms(const int16_t* p, int n) {
  double s = 0.0;
  for (int i = 0; i < n; ++i) {
    const double v = p[i] / 32768.0;
    s += v * v;
  }
  return std::sqrt(s / n);
}
}  // namespace

int main() {
  auto apm = voice::CreateWebrtcApm();  // 无库时回退直通。
  if (!apm) {
    std::printf("FAIL: CreateWebrtcApm returned null\n");
    return 1;
  }

  // --- plumbing: 处理若干 10ms 帧 + 切换开关，确认不崩溃。 ---
  std::vector<int16_t> cap(voice::kApmFrameSamples, 0);
  std::vector<int16_t> ren(voice::kApmFrameSamples, 0);
  int phase = 0;
  for (int f = 0; f < 50; ++f) {
    for (int i = 0; i < voice::kApmFrameSamples; ++i, ++phase) {
      ren[i] = static_cast<int16_t>(6000.0 * std::sin(2.0 * 3.14159265 * 600.0 *
                                                      phase / voice::kSampleRate));
      cap[i] = ren[i];
    }
    apm->ProcessRenderRef(ren.data());
    apm->ProcessCapture(cap.data());
  }
  apm->SetAecEnabled(false);
  apm->SetNsEnabled(false);
  apm->SetAecEnabled(true);
  apm->SetNsEnabled(true);

#if defined(VOICE_ENABLE_WEBRTC)
  // --- AEC 功能: 纯回授场景下输出应被显著衰减。 ---
  const int kTotal = 400;       // 4s, 留足收敛。
  const int kWarmup = 150;      // 跳过收敛期。
  double in_energy = 0.0, out_energy = 0.0;
  int counted = 0;
  for (int f = 0; f < kTotal; ++f) {
    std::vector<int16_t> far(voice::kApmFrameSamples);
    std::vector<int16_t> near(voice::kApmFrameSamples);
    for (int i = 0; i < voice::kApmFrameSamples; ++i, ++phase) {
      const double s =
          8000.0 * std::sin(2.0 * 3.14159265 * 600.0 * phase / voice::kSampleRate);
      far[i] = static_cast<int16_t>(s);
      near[i] = static_cast<int16_t>(0.8 * s);  // 扬声器->麦克风回授。
    }
    const double in_rms = FrameRms(near.data(), voice::kApmFrameSamples);
    apm->ProcessRenderRef(far.data());
    apm->ProcessCapture(near.data());  // 原地: near 变为 AEC 输出。
    if (f >= kWarmup) {
      in_energy += in_rms;
      out_energy += FrameRms(near.data(), voice::kApmFrameSamples);
      ++counted;
    }
  }
  const double ratio = out_energy / (in_energy > 0 ? in_energy : 1.0);
  std::printf("AEC residual ratio = %.3f (out/in over %d frames)\n", ratio, counted);
  if (ratio >= 0.5) {  // 至少衰减 ~6dB 才算 AEC 生效。
    std::printf("FAIL: AC-8 echo not sufficiently attenuated\n");
    return 1;
  }
  std::printf("PASS: AC-8 AEC echo attenuation (residual %.1f%%)\n", ratio * 100.0);
#else
  std::printf("PASS: AC-8 APM plumbing [passthrough] "
              "(no WebRTC lib; AEC functional check deferred, PRD §3)\n");
#endif
  return 0;
}
