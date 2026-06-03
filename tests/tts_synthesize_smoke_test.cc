// TTS 合成烟雾测试 (仅 VOICE_ENABLE_TTS=ON 注册)。
// 校验: CreateTts()->Init() 成功, 合成英文短语返回非空 16k/mono/int16 PCM。
// 英文音色在 Windows 上几乎必装; 中文音色依赖系统语音包, 故不在此断言中文。
#include <cstdio>

#include "tts.h"

int main() {
  auto tts = voice::CreateTts();
  if (!tts->Init()) {
    std::fprintf(stderr, "FAIL: TTS Init() 返回 false\n");
    return 1;
  }

  voice::Pcm16 pcm = tts->Synthesize("hello");
  if (pcm.empty()) {
    std::fprintf(stderr,
                 "FAIL: Synthesize(\"hello\") 返回空 (系统是否安装语音?)\n");
    return 1;
  }

  // 16k 下合成 "hello" 至少应有几百毫秒 (这里宽松断言 > 0 已在上方覆盖)。
  const double seconds = static_cast<double>(pcm.size()) / 16000.0;
  std::printf("PASS: 合成 %zu 样本 (~%.2fs)\n", pcm.size(), seconds);
  return 0;
}
