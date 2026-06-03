# 设计：文字转语音（/say 命令）

- 日期：2026-06-03
- 状态：已批准（待实现计划）
- 关联：PRD §（音频下行链路）、CLAUDE.md 接口契约、AC-1 文本闭环、AC-9 参数实时

## 1. 目标

在现有 Loopback 测试客户端里增加「打字 → 合成语音 → 走音频下行链路 → 播放」的能力，
通过 `/say <文字>` 命令触发。用于在无真实后端、无大模型的前提下，把文本变成
可听的语音并验证 Opus 编码 / Loopback 回传 / 解码 / jitter / 播放这条下行管线。

## 2. 范围与非目标

**范围**
- 新增 `/say <text>` 命令：文本经 TTS 合成为 16k/mono/int16 PCM，喂入下行链路播放。
- 新增可插拔 TTS 接口 + Windows SAPI5 实现 + 非 Windows/关闭时的 stub。
- CMake 开关 `VOICE_ENABLE_TTS`（默认 OFF）。

**非目标（YAGNI）**
- 不做选音色 / 语速 / 音量 UI；用系统默认语音。
- 不引入在线 TTS、大模型或网络依赖。
- 不改 GUI（ImGui 当前未集成）；仅留 `OnSpeak` 入口供将来 GUI 调用。
- 不让合成音走上行（麦克风采集 / APM / VAD）路径。

## 3. 硬性约束对齐（CLAUDE.md）

- 全链路 PCM 统一 **16kHz / 单声道 / int16**：SAPI 直接输出 `SPSF_16kHz16BitMono`，
  无需重采样；常量用 `src/audio_constants.h`，不写魔数。
- 接口契约不变：复用 `ITransport`(`LoopbackTransport`)、`OpusCodec`、`AudioPlayback`，
  不改其调用方。新增 `ITextToSpeech` 作为新接口，按「接口 + stub」惯例提供实现。
- 实时回调禁阻塞等约束不受影响：`OnSpeak` 在 UI/控制线程执行（非实时回调），
  `SendAudio` 仅入队，解码/播放在各自线程。
- 中文不乱码：源码 UTF-8 + `/utf-8`；文本以 UTF-8 传入，SAPI 前转 UTF-16。
- 无第三方库也能编译：`VOICE_ENABLE_TTS` 默认 OFF 时用 `NullTts` stub。

## 4. 数据流

```
/say 你好
  └─► ui.cc headless 循环解析 "/say " 前缀
        └─► ChatController::OnSpeak("你好")
              ├─ transport_->SetDelayMs(settings_.loopback_delay_ms)   // AC-9 延迟下发
              ├─ log_.Push(MessageKind::kMe, "/say 你好")
              ├─ Pcm16 pcm = tts_->Synthesize("你好")                   // 空=失败/不支持
              │     └─ 空 → log_.Push(kLog, "TTS: synth failed/unsupported"); return
              ├─ pad pcm 到 kOpusFrameSamples(320) 的整数倍（尾帧补零）
              └─ EmitFrames(pcm)                                        // 复用现有下行
                    └─ 切320 → codec_->Encode → transport_->SendAudio
                          └─ Loopback 延迟回传 → on_audio
                                └─ codec_->Decode → playback_->Enqueue → jitter → 扬声器
```

跳过麦克风采集与 VAD，对应用户选定的「下行」路径。

## 5. 模块设计

### 5.1 新增 `src/tts.h`

```cpp
#pragma once
#include <memory>
#include <string>
#include "types.h"  // Pcm16

namespace voice {

class ITextToSpeech {
 public:
  virtual ~ITextToSpeech() = default;
  virtual bool Init() = 0;
  // UTF-8 文本 → 16k/mono/int16 PCM；失败或不支持返回空 Pcm16。
  virtual Pcm16 Synthesize(const std::string& utf8_text) = 0;
};

std::unique_ptr<ITextToSpeech> CreateTts();

}  // namespace voice
```

### 5.2 新增 `src/tts.cc`

- `#if defined(VOICE_ENABLE_TTS) && defined(_WIN32)`：`Sapi5Tts`
  - `Init()`：`CoInitializeEx`（与合成同线程）；`CoCreateInstance(CLSID_SpVoice)`。
  - `Synthesize()`：
    1. `MultiByteToWideChar(CP_UTF8, ...)` 把 UTF-8 转 UTF-16。
    2. 建内存 `IStream`（`CreateStreamOnHGlobal`）+ `ISpStream`，
       `SetBaseStream(stream, SPDFID_WaveFormatEx, &wfx)`，wfx = 16k/mono/16bit，
       或用 `SpBindToFile`/格式枚举 `SPSF_16kHz16BitMono`。
    3. `voice->SetOutput(spStream, TRUE)`；`voice->Speak(wtext, SPF_DEFAULT, nullptr)`（同步）。
    4. 从 base stream 读出 raw PCM 字节（`SetBaseStream + WaveFormatEx` 写出的是无 RIFF 头
       的裸 PCM），转 `Pcm16` 返回。
  - 失败任一步返回空并允许上层记日志。
- `#else`：`NullTts`，`Init()` 返回 true，`Synthesize()` 首次调用记一次
  「TTS unsupported（未启用 VOICE_ENABLE_TTS 或非 Windows）」并返回空。
- `CreateTts()` 按宏选择实现。

### 5.3 `ChatController` 改动

- 头文件加成员 `std::unique_ptr<ITextToSpeech> tts_;` 与方法 `void OnSpeak(const std::string& text);`
- `Init()`：`tts_ = CreateTts(); tts_->Init();`（失败不致命，记日志，后续 Synthesize 返回空）。
- `OnSpeak()`：见 §4；为复用 `EmitFrames`（其按 320 切帧、余数留缓冲），合成 PCM
  先补零到 320 整数倍再传入临时缓冲，确保整段都被发出。

### 5.4 `ui.cc` headless 循环

在现有 `/mic`、`/quit` 分支旁加：
```cpp
} else if (line.rfind("/say ", 0) == 0) {
  controller->OnSpeak(line.substr(5));
}
```
普通文本仍走 `OnTextSubmit`（AC-1 不变）。更新提示行，列出 `/say`。

## 6. CMake

- 根 `CMakeLists.txt`：`option(VOICE_ENABLE_TTS "Enable Windows SAPI5 TTS" OFF)`；
  ON 时 `target_compile_definitions(... VOICE_ENABLE_TTS)` 且 Windows 下链 `ole32`
  （SAPI 经 COM；如需 `sapi.h` 头，确保 Windows SDK include 可用）。
- `src/CMakeLists.txt`：加入 `tts.cc`（始终编译，内部按宏切换）。
- `CMakePresets.json`：新增 `tts` configure/build preset（继承 vs2019-base，
  `VOICE_ENABLE_TTS=ON`，可叠加 `VOICE_ENABLE_OPUS=ON` 以走真实 Opus 下行）；
  并把 `VOICE_ENABLE_TTS=ON` 加进 `all` preset。

## 7. 线程与生命周期

- `OnSpeak` 在 UI/控制线程（headless 主线程）执行，与 `controller.Init()` 同线程，
  满足 SAPI COM「初始化与调用同线程」要求。
- SAPI 同步 `Speak` 短暂阻塞该线程可接受（非实时音频回调）。
- 析构：`Sapi5Tts` 释放 COM 指针并 `CoUninitialize`（仅当本类成功 `CoInitializeEx`）。

## 8. 错误处理

- TTS 未启用 / 非 Windows / 无可用语音 / 合成失败：`Synthesize` 返回空，
  `OnSpeak` 记 `[log] TTS: ...` 且不发音频，不崩溃。
- 中文无语音包：可能返回空或读为英文——属系统语音配置，记日志提示，不在本特性范围内修复。

## 9. 测试

- `tts_synthesize_smoke`（仅 `VOICE_ENABLE_TTS` ON 编译）：`CreateTts()->Init()`，
  `Synthesize("hello")` 断言返回样本数 > 0（英文音色几乎必装）。
- 下行链路（Opus/Loopback/解码/jitter/播放）已由 `m2_monitor_loopback`、
  `ac4_utterance_loopback` 覆盖；`OnSpeak` 复用 `EmitFrames`，不另加集成测试。
- 验证：`tts` preset 配置+编译通过；手测 `/say hello` 能听到回放。

## 10. 已知限制

- 中文朗读依赖系统中文语音包（Win11 多数自带；缺失则只有英文音色）。
- SAPI5 音色较老；如需更自然音色可后续切 WinRT SpeechSynthesis（非本次范围）。
```
