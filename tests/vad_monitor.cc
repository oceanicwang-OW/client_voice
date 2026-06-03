// 实时 VAD 探针 (手动诊断, 非自动化测试)。
//
// 复现 ChatController 上行链路: 采集 -> APM(160) -> VAD(512), 实时打印语音
// 概率与 speech start/end 事件, 用于定位 "/mic 不回传" 卡在哪一步。
//
// 用法:
//   vad_monitor              使用 WebRTC APM (与全功能 build 一致)
//   vad_monitor passthrough  使用直通 APM (隔离 WebRTC APM 的影响)
//
// 观察: 说话时 prob 应升高越过阈值(0.5)触发 START; 停顿后 prob 应回落, 累计
// 静音达 600ms 触发 END。若停顿后 prob 一直高 -> APM(AGC) 把底噪放大导致永不
// 句末; 若说话时 prob 也上不去 -> 采集/APM 把语音削没了。
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "audio_capture.h"
#include "audio_constants.h"
#include "audio_processor.h"
#include "vad.h"

#ifndef VOICE_SILERO_MODEL_PATH
#define VOICE_SILERO_MODEL_PATH "models/silero_vad.onnx"
#endif

int main(int argc, char** argv) {
  using namespace std::chrono_literals;
  using namespace voice;

  const bool use_passthrough = (argc > 1 && std::strcmp(argv[1], "passthrough") == 0);

  AudioCapture capture;
  auto apm = use_passthrough ? CreatePassthroughApm() : CreateWebrtcApm();
  apm->SetAecEnabled(true);
  apm->SetNsEnabled(true);

  VadDetector vad;
  const bool loaded = vad.Load(VOICE_SILERO_MODEL_PATH);
  vad.SetThreshold(kVadDefaultThreshold);
  vad.SetMinSilenceMs(kVadDefaultMinSilenceMs);

  std::printf("APM: %s | Silero 模型: %s | 阈值 %.2f | 句末静音 %dms\n",
              use_passthrough ? "passthrough" : "WebRTC",
              loaded ? "已加载" : "未加载(!!)",
              static_cast<double>(kVadDefaultThreshold),
              kVadDefaultMinSilenceMs);

  if (!capture.Start()) {
    std::printf("SKIP: 无采集设备\n");
    return 0;
  }
  std::printf("跑 ~20 秒。请说一句话, 然后停顿 2 秒, 反复几次。观察 prob 与事件:\n\n");

  Pcm16 chunk, apm_accum, vad_accum;
  float max_prob = 0.0f, min_prob = 1.0f;
  int starts = 0, ends = 0;
  const auto end = std::chrono::steady_clock::now() + 20s;

  while (std::chrono::steady_clock::now() < end) {
    capture.Read(&chunk, 4096);
    if (chunk.empty()) {
      std::this_thread::sleep_for(5ms);
      continue;
    }
    apm_accum.insert(apm_accum.end(), chunk.begin(), chunk.end());

    size_t off = 0;
    while (apm_accum.size() - off >= kApmFrameSamples) {
      apm->ProcessCapture(apm_accum.data() + off);
      vad_accum.insert(vad_accum.end(), apm_accum.data() + off,
                       apm_accum.data() + off + kApmFrameSamples);
      off += kApmFrameSamples;
    }
    apm_accum.erase(apm_accum.begin(), apm_accum.begin() + off);

    size_t voff = 0;
    while (vad_accum.size() - voff >= kVadWindowSamples) {
      const VadEvent ev = vad.Push(vad_accum.data() + voff);
      voff += kVadWindowSamples;
      const float p = vad.last_prob();
      if (p > max_prob) max_prob = p;
      if (p < min_prob) min_prob = p;

      if (ev == VadEvent::kSpeechStart) {
        ++starts;
        std::printf("\n>> SPEECH START  (prob=%.2f)\n", static_cast<double>(p));
      } else if (ev == VadEvent::kSpeechEnd) {
        ++ends;
        std::printf("\n>> SPEECH END    (prob=%.2f)\n", static_cast<double>(p));
      }

      const int bars = static_cast<int>(p * 40.0f);
      char meter[41];
      for (int i = 0; i < 40; ++i) meter[i] = (i < bars) ? '#' : '.';
      meter[40] = '\0';
      std::printf("\rprob %.2f |%s|", static_cast<double>(p), meter);
      std::fflush(stdout);
    }
    vad_accum.erase(vad_accum.begin(), vad_accum.begin() + voff);
    std::this_thread::sleep_for(5ms);
  }
  capture.Stop();

  std::printf("\n\n概率范围: min=%.2f  max=%.2f | START=%d  END=%d\n",
              static_cast<double>(min_prob), static_cast<double>(max_prob),
              starts, ends);
  if (max_prob < kVadDefaultThreshold) {
    std::printf("诊断: 说话时概率都没越过阈值 -> 语音被削弱或没采到 (查 APM/采集)。\n");
  } else if (ends < starts) {
    std::printf("诊断: 有 START 但 END 偏少 -> 停顿时概率没回落, APM 放大底噪嫌疑 "
                "(对比 passthrough)。\n");
  } else {
    std::printf("诊断: START/END 成对 -> VAD 正常, /mic 应能回传。\n");
  }
  return 0;
}
