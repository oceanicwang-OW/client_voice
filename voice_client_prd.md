# 语音/文本客户端 闭环测试 PRD

> 面向 AI 编码的需求文档。目标：用 C++ 实现一个具备「文字输入 + 自由语音」的简单客户端，跑通本地音频闭环（采集 → 处理 → 编码 → 回环 → 解码 → 播放），**不接入真实 AIServer，用本地 Loopback 替代网络收发**。

---

## 1. 目标与范围

### 1.1 目标
实现一个桌面端测试程序，验证「持续监听语音 + 文本输入」这条交互链路的客户端部分是否成立。不需要 AI 大模型、不需要语音识别、不需要真实服务端——服务端用一个本地回环（Loopback）模块替代，把"发出去的音频原样（或带延迟地）还回来"，从而在没有后端的情况下完成端到端验证。

### 1.2 在范围内（In Scope）
- 简单 UI：文本输入框 + 发送按钮 + 「开麦/闭麦」按钮 + 消息/日志显示区 + 音量/VAD 状态指示。
- 音频采集（持续监听，非按住录音）。
- 回声消除 + 降噪（AEC/NS/AGC）。
- VAD 实时端点检测（判断一句话的开始与结束）。
- Opus 编解码。
- Loopback 传输模块（替代 WebSocket + AIServer）。
- 播放缓冲队列（jitter buffer）+ 扬声器播放。
- Barge-in：播放过程中用户开口可打断播放。

### 1.3 不在范围内（Out of Scope）
- 真实 WebSocket 连接与协议设计（仅预留接口）。
- AIServer、LLM、知识库、ASR、TTS（全部不在客户端，本测试不实现）。
- 用户账号、鉴权、持久化存储。
- 移动端、跨平台打包分发（本期仅 Windows）。

### 1.4 关键澄清
- 客户端**不包含任何大模型**。唯一的「模型文件」是 Silero VAD 的微型检测网络（约 1~2 MB），仅用于判断「当前是否有人在说话」，无语言/生成能力。
- 若要做到客户端**零模型文件**，可将 Silero VAD 替换为 WebRTC VAD（纯 DSP，随 WebRTC APM 附带）。见 §11 可选项。

---

## 2. 开发环境与编码规范

### 2.1 开发环境
| 项 | 要求 |
|---|---|
| 操作系统 | Windows 10 / 11，64 位 |
| IDE / 编译器 | Visual Studio 2019（MSVC v142 工具集） |
| C++ 标准 | C++17（编译选项 `/std:c++17`） |
| 目标架构 | x64 |
| 构建系统 | CMake (>= 3.20)，生成 VS2019 解决方案：`cmake -G "Visual Studio 16 2019" -A x64 ..` |
| 字符集 | UTF-8（源码与字符串统一 UTF-8；MSVC 加 `/utf-8`，避免中文乱码） |

> 所有依赖库须能在 MSVC v142 / x64 下编译或提供 x64 预编译产物。WebRTC APM 在 Windows 上建议走 vcpkg 的 `webrtc-audio-processing` 或预编译，避免本机 Meson 构建链问题。

### 2.2 编码规范
- **项目自有代码**：遵循 Google C++ Style Guide（https://google.github.io/styleguide/cppguide.html）。关键约定：
  - 命名：类型 `PascalCase`；函数/方法 `PascalCase`；变量 `snake_case`；类成员变量 `snake_case_`（带尾下划线）；常量 `kConstantName`；宏全大写下划线。
  - 缩进 2 空格，不用 Tab；行宽 80（团队可统一放宽到 100，但须一致）。
  - 头文件用 `#pragma once`（或 `#define` guard，全项目统一一种）。
  - 优先现代 C++：`nullptr`、`std::unique_ptr` / `std::shared_ptr`、`override`、`constexpr`、`enum class`、`auto`（适度）。
  - 注释、函数文档、`TODO(name):` 格式遵循 Google 规范；接口注释写在头文件。
  - 异常使用从简（音频实时路径避免抛异常）；错误用返回值/状态码传递。
- **第三方/开源库代码**（miniaudio、Dear ImGui、libopus、webrtc-audio-processing、ONNX Runtime、GLFW 等）：**保持各库原有代码风格，原样纳入**，不重排格式、不套用 Google 规范，以便后续升级合并。
- Google 规范**仅作用于本项目自有代码**，包括「对开源库的封装/适配层」（如 `audio_capture.cpp` 等）；开源库源码本体不动。
- 建议提供 `.clang-format`（`BasedOnStyle: Google`）仅作用于 `src/`，并在 `third_party/` 放置 `.clang-format` 内容为 `DisableFormat: true` 或将其排除，防止误格式化开源库。

---

## 3. 技术选型（全部开源、C/C++）

| 能力 | 库 | 许可证 | 语言 | 来源 |
|---|---|---|---|---|
| 音频采集/播放/重采样 | miniaudio | Public Domain / MIT-0 | C（C++ 直接用） | https://github.com/mackron/miniaudio （单头文件） |
| 回声消除/降噪/增益 | WebRTC APM | BSD | C++ | https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing （standalone 打包版） |
| VAD 端点检测 | Silero VAD | MIT | 推理用 C++（onnxruntime） | https://github.com/snakers4/silero-vad （模型）+ ONNX Runtime |
| ONNX 推理引擎 | ONNX Runtime | MIT | C++ | https://github.com/microsoft/onnxruntime |
| 音频编解码 | libopus | BSD | C（C++ 直接用） | https://github.com/xiph/opus |
| UI | Dear ImGui | MIT | C++ | https://github.com/ocornut/imgui （后端用 GLFW + OpenGL3） |
| 窗口/GL 上下文 | GLFW | Zlib | C | https://github.com/glfw/glfw |
| 构建 | CMake (>= 3.20) | — | — | 生成 VS2019 工程；依赖推荐 vcpkg 或 FetchContent |

> **依赖难度提示**：WebRTC APM 是最难编译的依赖。允许在阶段一先用一个「直通 stub」实现 `IAudioProcessor` 接口（不做任何处理），阶段二再接入真正的 WebRTC APM。其余库集成都很轻。

---

## 4. 总体架构

程序分为「上行链路」和「下行链路」两条数据流，中间用 Loopback 模块连接（代替网络 + AIServer）。

```
                          ┌───────────────────────────┐
   麦克风 ──► 采集 ──► AEC/降噪 ──► VAD 端点检测 ──► Opus 编码 ──► [Loopback 发送]
            (capture)   (APM)     (Silero VAD)      (libopus)         │
                                      │                               │ 把编码帧原样/延迟回传
                                      │ 检测到用户开口                 ▼
                                      │ ──► 打断播放/清空缓冲    [Loopback 接收]
                                      ▼                               │
   扬声器 ◄── 播放 ◄── 播放缓冲队列 ◄── Opus 解码 ◄────────────────────┘
            (playback)  (jitter buffer)  (libopus)
```

- 文本路径独立且简单：UI 文本框 → ChatController → Loopback（文本回环，原样/加前缀回显）→ UI 消息区。
- 语音「闭环」= 用户说话 → 走完上行 → Loopback 把这段语音还回来 → 走完下行 → 用户听到自己刚说的话。这验证了整条音频链路。

---

## 5. UI 需求

技术：Dear ImGui + GLFW + OpenGL3。单窗口，固定布局即可，不要求美观，要求功能可观测。

### 5.1 布局（单窗口，从上到下）
1. **消息/日志区**（可滚动，占主要空间）：显示文本消息（`Me: ...` / `Echo: ...`）与系统日志（如 `VAD: speech start`、`VAD: speech end (1.2s)`、`Playback: queued 1.2s audio`）。
2. **文本输入行**：单行文本框 + 「发送」按钮。回车也触发发送。
3. **语音控制行**：
   - 「开麦 / 闭麦」切换按钮（显示当前状态）。
   - 实时电平条（输入音量 RMS）。
   - VAD 状态灯：静音=灰，说话中=绿。
   - 播放状态灯：空闲=灰，播放中=蓝。
4. **设置区（可折叠）**：
   - VAD 阈值滑块（0.0~1.0，默认 0.5）。
   - 句末静音时长滑块（200~1500 ms，默认 600）。
   - AEC 开关、降噪开关。
   - Loopback 延迟滑块（0~1000 ms，模拟网络/服务端处理延迟，默认 300）。
   - 监听模式切换：`Utterance Loopback`（默认）/ `Realtime Monitor`（见 §7.2）。

### 5.2 UI 交互要求（功能性）
- 「开麦」后立即进入持续监听（不是按住录音）；「闭麦」后停止采集并清空上行缓冲。
- 文本发送：把内容追加到消息区为 `Me:`，Loopback 回显为 `Echo:`。
- 语音闭环触发时，在消息区打印一条 `Voice: <时长>s utterance looped back`，并自动播放。
- 所有状态变化（VAD、播放、打断）都要打日志，便于观测验证。

---

## 6. 功能需求（FR）

| 编号 | 需求 | 验收要点 |
|---|---|---|
| FR-1 | 持续采集麦克风音频，重采样到 16 kHz / 单声道 / 16-bit | 开麦后电平条实时跳动 |
| FR-2 | 对采集音频做 AEC + 降噪（可开关） | 开扬声器播放时回授明显减弱 |
| FR-3 | 用 Silero VAD 实时检测语音起止（端点检测） | 说话时 VAD 灯变绿，停顿超过阈值后判定句末 |
| FR-4 | 一句话结束后，将该段 PCM 用 Opus 编码 | 日志打印编码帧数/字节数 |
| FR-5 | Loopback 模块在设定延迟后回传编码帧（替代网络+AIServer） | 可调延迟，回传完整无丢帧 |
| FR-6 | 接收回传帧 → Opus 解码 → 进播放缓冲队列 | 解码后样本数与编码前一致（±一帧） |
| FR-7 | 播放缓冲队列平滑喂给扬声器播放 | 播放无明显卡顿/爆音 |
| FR-8 | Barge-in：播放期间检测到用户开口，立即停止播放并清空缓冲 | 播放中说话能立刻打断 |
| FR-9 | 文本输入闭环：发送文本经 Loopback 回显到消息区 | 文本即时回显 |
| FR-10 | 全程关键事件打日志到 UI 消息区 | 可凭日志复现整条链路 |

---

## 7. 闭环（Loopback）测试设计

### 7.1 Utterance Loopback（默认模式，模拟"发一句、收一句语音回复"）
1. 用户开麦，持续说话。
2. VAD 判定句子结束（trailing silence ≥ 阈值）。
3. 把这一整句的处理后 PCM 切成 20ms 帧，Opus 编码。
4. 把编码帧交给 `LoopbackTransport`，它在「Loopback 延迟」后原样回传这些帧。
5. 接收端解码 → 推入 jitter buffer → 播放。
6. 结果：用户说完一句话，约延迟后听到自己刚说的那句话被"回复"出来。

> 这是最贴近真实场景（发语音→收到 AI 语音回复）的闭环，验证整条上下行链路。

### 7.2 Realtime Monitor（可选模式，纯延迟/回授测试）
采集帧经 AEC 处理后，**不经过 VAD 断句**，直接编码→Loopback→解码→播放，形成近实时回声。用于测端到端延迟和 AEC 效果。

### 7.3 测试注意事项
- 建议**戴耳机测试**，避免外放时自己声音被反复采集形成自激（即便有 AEC）。
- Loopback 不丢包、不乱序（除非显式加入抖动用于测试 jitter buffer）。

---

## 8. 关键音频参数（实现必须遵守）

| 参数 | 值 | 说明 |
|---|---|---|
| 采样率 | 16000 Hz | 全链路统一；采集设备若非 16k 必须重采样 |
| 声道 | 1（单声道） | |
| 采样格式 | 16-bit signed PCM（处理内部可用 float） | |
| APM 处理帧 | 10 ms = 160 samples | WebRTC APM 固定按 10ms 帧处理 |
| VAD 输入窗 | 512 samples（32 ms）@16k | Silero VAD v4/v5 要求；按模型版本适配 |
| VAD 阈值 | 默认 0.5（可调 0~1） | speech 概率 > 阈值 视为有声 |
| 句首确认 | 连续 ≥ 2 个有声窗 才算 speech start | 抑制瞬时噪声误触发 |
| 句末静音 | 默认 600 ms（可调 200~1500） | 连续静音达此时长判 speech end |
| 最短语音 | 300 ms | 短于此的"语音"丢弃（误触发） |
| Opus 编码帧 | 20 ms = 320 samples | 语音常用帧长 |
| Opus 模式 | `OPUS_APPLICATION_VOIP` | |
| Opus 码率 | 24000 bps（可调） | |
| Jitter buffer | 目标 60~120 ms 预缓冲 | 攒够再起播，避免欠载 |

> 各模块帧长不同（采集任意 → APM 10ms → VAD 512samples → Opus 20ms），实现时必须用**环形缓冲 + 重新切帧**在模块间对齐，不能假设上一级输出正好等于下一级输入。

---

## 9. 模块划分与接口（C++ 头文件草图）

> 接口为建议，AI 实现时可微调命名，但职责边界应保持。命名遵循 §2.2 Google 规范。所有 PCM 约定为 16k/mono/int16。

### 9.1 公共类型
```cpp
// types.h
#include <cstdint>
#include <vector>

using Pcm16 = std::vector<int16_t>;          // 16k mono int16
using OpusPacket = std::vector<uint8_t>;     // 单个 Opus 编码帧

struct AudioConfig {
  int sample_rate = 16000;
  int channels = 1;
};
```

### 9.2 采集
```cpp
// audio_capture.h  —— 基于 miniaudio
class AudioCapture {
 public:
  bool Start();   // 打开默认输入设备，启动持续采集
  void Stop();
  // 采集回调里只做：重采样到16k/mono + 写入无锁环形缓冲。绝不阻塞。
  // 处理线程通过 Read() 取出连续 PCM。
  size_t Read(Pcm16* out, size_t max_samples);
  bool IsRunning() const;
};
```

### 9.3 音频处理（AEC/NS/AGC）
```cpp
// audio_processor.h  —— 基于 WebRTC APM；阶段一可用直通 stub
class IAudioProcessor {
 public:
  virtual ~IAudioProcessor() = default;
  // 处理一帧 10ms(160 samples) 近端采集音频，原地修改
  virtual void ProcessCapture(int16_t* frame_160) = 0;
  // 提供远端(将要播放)的一帧 10ms，供 AEC 做参考
  virtual void ProcessRenderRef(const int16_t* frame_160) = 0;
  virtual void SetAecEnabled(bool enabled) = 0;
  virtual void SetNsEnabled(bool enabled) = 0;
};
// 工厂：CreateWebrtcApm() / CreatePassthroughApm()
```

> AEC 需要"远端参考信号"= 即将从扬声器播放的音频。实现时播放链路要把每一帧也喂给 `ProcessRenderRef`，二者按 10ms 帧对齐。

### 9.4 VAD 端点检测
```cpp
// vad.h  —— 基于 Silero VAD (onnxruntime)
enum class VadEvent { kNone, kSpeechStart, kSpeechEnd };

class VadDetector {
 public:
  bool Load(const std::string& onnx_model_path);
  // 送入 512 samples(32ms)，返回是否触发起/止事件
  // 内部维护状态机：IDLE / SPEAKING + trailing-silence 计数
  VadEvent Push(const int16_t* frame_512);
  void SetThreshold(float t);
  void SetMinSilenceMs(int ms);
  void Reset();
  float last_prob() const;     // 供 UI 显示
};
```

### 9.5 编解码
```cpp
// codec.h  —— 基于 libopus
class OpusCodec {
 public:
  bool Init(int sample_rate = 16000, int channels = 1, int bitrate = 24000);
  // 输入 320 samples(20ms)，输出一个 Opus 包
  OpusPacket Encode(const int16_t* frame_320);
  // 输入一个 Opus 包，输出 320 samples
  Pcm16 Decode(const OpusPacket& pkt);
};
```

### 9.6 Loopback 传输（替代 WebSocket+AIServer）
```cpp
// transport.h
#include <functional>
#include <string>

class ITransport {
 public:
  virtual ~ITransport() = default;
  virtual void SendAudio(const OpusPacket& pkt) = 0;
  virtual void SendText(const std::string& text) = 0;
  // 回调：收到（回传的）音频帧 / 文本
  std::function<void(const OpusPacket&)> on_audio;
  std::function<void(const std::string&)> on_text;
  // 句子结束信号，便于接收端判断一段回复结束（可选）
  std::function<void()> on_audio_end;
};
// LoopbackTransport: 把 SendAudio/SendText 的内容，在设定延迟后
// 通过 on_audio/on_text 原样回传（文本可加 "Echo: " 前缀）。
// 用一个定时/延迟队列实现，运行在独立线程。
```

> 真实接入服务端时，只需新增一个 `WebSocketTransport` 实现同一个 `ITransport` 接口替换即可，其余代码不动。

### 9.7 播放与缓冲
```cpp
// audio_playback.h  —— jitter buffer + miniaudio 播放
class AudioPlayback {
 public:
  bool Start();
  void Stop();
  void Enqueue(const Pcm16& pcm);   // 解码后的 PCM 入队
  void Flush();                     // barge-in 时清空并停止当前播放
  bool IsPlaying() const;
  // 播放回调里：从队列取数据填充输出缓冲；不足时填静音。
  // 攒够预缓冲(目标60~120ms)再起播。
  // 每填一帧 10ms，同步喂给 IAudioProcessor::ProcessRenderRef 作 AEC 参考。
};
```

### 9.8 控制器
```cpp
// chat_controller.h
class ChatController {
 public:
  void OnTextSubmit(const std::string& text);  // 文本路径
  void OnMicToggle(bool on);                    // 开/闭麦
  // 内部驱动处理线程：Read 采集 → APM(10ms) → VAD(512) → 攒句 →
  //   kSpeechEnd 时切20ms编码 → transport.SendAudio
  // transport.on_audio → Decode → playback.Enqueue
  // VAD kSpeechStart 且 playback.IsPlaying() → playback.Flush() (barge-in)
};
```

---

## 10. 线程模型与数据流

| 线程 | 职责 | 约束 |
|---|---|---|
| 采集回调线程（miniaudio） | 重采样 + 写环形缓冲 | 不得阻塞、不分配大内存、不打日志 |
| 播放回调线程（miniaudio） | 从 jitter buffer 取数据填充输出 + 喂 AEC 参考帧 | 同上 |
| 处理线程（自建） | 读采集 → APM → VAD → 攒句 → 编码 → 发送；并处理 transport 回调解码入队 | 可阻塞，承载主要逻辑 |
| Loopback 线程 | 延迟回传 | 简单定时队列 |
| UI/主线程 | ImGui 渲染、按钮事件、读取状态显示 | 与处理线程间用无锁队列/原子量通信 |

**线程安全要点**
- 采集/播放与处理线程之间用**单生产者单消费者无锁环形缓冲**。
- UI 与处理线程之间：状态用 `std::atomic`，日志消息用一个加锁的小队列。
- Barge-in 路径要快：VAD kSpeechStart → 直接调用 `playback.Flush()`，并通过 transport 发一个 cancel（本测试 Loopback 里清空其待回传队列即可）。

---

## 11. 可选项 / 降级路径

- **零模型文件**：用 WebRTC VAD（随 webrtc-audio-processing 附带，纯 DSP）替换 `VadDetector` 实现，接口不变。代价：嘈杂环境准确率下降。
- **跳过 AEC（阶段一）**：用 `CreatePassthroughApm()`，先跑通链路。注意此时务必戴耳机测试，否则外放会自激。
- **跳过 Opus（阶段一）**：Loopback 直接传 PCM，先验证采集→VAD→播放，再补编解码。
- **跳过 VAD 断句**：用 §7.2 Realtime Monitor 模式先验证采集→播放延迟与 AEC。

---

## 12. 构建与依赖

- 平台与工具链：见 §2.1（Windows / VS2019 / MSVC v142 / x64 / C++17）。
- 构建系统：CMake (>= 3.20)，生成 VS2019 解决方案：
  ```
  cmake -G "Visual Studio 16 2019" -A x64 -B build .
  cmake --build build --config Release
  ```
- 依赖获取（Windows 推荐）：
  - **vcpkg**：`opus`、`webrtc-audio-processing`、`onnxruntime`（或用官方预编译）、`glfw3`。
  - **源码内嵌**：miniaudio（单头 `miniaudio.h`）、Dear ImGui（`imgui/` 源文件 + GLFW/OpenGL3 后端）。
- MSVC 编译选项建议：`/std:c++17 /utf-8 /W4`（third_party 可降低告警级别）。
- Silero VAD 模型文件（`silero_vad.onnx`）随程序分发到 `models/`，路径可配置；onnxruntime 的 DLL 需随可执行文件一起部署。

建议目录结构：
```
voice-client/
├── CMakeLists.txt
├── .clang-format            # BasedOnStyle: Google，仅作用于 src/
├── third_party/             # miniaudio.h, imgui/ ...（保持原风格，排除格式化）
├── models/                  # silero_vad.onnx
├── src/
│   ├── types.h
│   ├── audio_capture.{h,cc}
│   ├── audio_processor.{h,cc}    # webrtc apm + passthrough
│   ├── vad.{h,cc}                # silero vad (onnxruntime)
│   ├── codec.{h,cc}              # libopus
│   ├── transport.{h,cc}          # loopback transport
│   ├── audio_playback.{h,cc}     # jitter buffer + playback
│   ├── chat_controller.{h,cc}
│   ├── ring_buffer.h             # SPSC 无锁环形缓冲
│   ├── ui.{h,cc}                 # imgui 界面
│   └── main.cc
└── README.md
```

> 文件扩展名 `.cc`/`.h` 遵循 Google 规范；若团队偏好 `.cpp` 可全项目统一，但须一致。

---

## 13. 验收标准（测试用例）

| 用例 | 步骤 | 期望结果 |
|---|---|---|
| AC-1 文本闭环 | 输入文字点发送 | 消息区出现 `Me: xxx` 和 `Echo: xxx` |
| AC-2 持续监听 | 点开麦后保持不动 | 电平条随环境音跳动，VAD 灯保持灰（无误触发） |
| AC-3 端点检测 | 说一句话后停顿 | VAD 灯说话时变绿，停顿约 600ms 后打印 `speech end` |
| AC-4 语音闭环 | 说"测试一二三"后停 | 约(延迟)后听到自己说的"测试一二三"，消息区有 `Voice: x.xs looped back` |
| AC-5 编解码完整性 | 观察日志 | 解码后样本数 ≈ 编码前（误差 ≤ 1 帧） |
| AC-6 播放平滑 | 多次语音闭环 | 播放无卡顿/爆音/明显断续 |
| AC-7 Barge-in | 闭环音频播放中开口说话 | 播放立即停止、缓冲清空，日志打印 `barge-in: playback flushed` |
| AC-8 AEC（接入后） | 外放播放闭环音频时不说话 | 不产生自激/回授循环；VAD 不被扬声器声音误触发 |
| AC-9 参数实时生效 | 调整 VAD 阈值/静音时长滑块 | 断句灵敏度随之变化 |
| AC-10 环境与规范 | 检查工程与代码 | VS2019 x64 可一键编译；自有代码符合 Google 规范，third_party 未被改格式 |

---

## 14. 里程碑（建议实现顺序）

1. **M1 框架与文本闭环**：CMake(VS2019) + ImGui 窗口 + LoopbackTransport（文本）。达成 AC-1。
2. **M2 采集与播放直通**：miniaudio 采集→（PCM 直接）Loopback→播放（Realtime Monitor）。达成 AC-2、AC-6 基础、§7.2。
3. **M3 接入 Opus**：上行编码、下行解码。达成 AC-5。
4. **M4 接入 VAD 与断句**：Silero VAD + 端点状态机 + Utterance Loopback。达成 AC-3、AC-4。
5. **M5 Barge-in**：VAD kSpeechStart 触发 Flush + cancel。达成 AC-7。
6. **M6 接入 WebRTC APM**：替换 passthrough，加 render 参考帧对齐。达成 AC-8。
7. **M7 打磨**：参数面板、jitter buffer 调优、日志完善、规范自查。达成 AC-9、AC-10。

---

## 15. 给实现者的关键提醒（最容易出错处）

1. **帧长对齐**：采集→APM(10ms)→VAD(512samples)→Opus(20ms) 帧长都不同，模块间必须靠环形缓冲重新切帧，别假设直通。
2. **AEC 参考信号**：播放的每一帧也要喂给 APM 的 render 接口，且与采集帧时间对齐，否则 AEC 无效。
3. **实时回调禁阻塞**：采集/播放回调里只搬数据，重活全在处理线程。
4. **Barge-in 要快且彻底**：Flush 播放缓冲 + 清空 Loopback 待回传队列 + 复位 VAD 状态。
5. **戴耳机测试**：尤其在 AEC 未接入或外放时，避免自激干扰判断。
6. **Silero VAD 输入尺寸**：严格按模型版本要求的样本数喂（v4/v5 为 512@16k），尺寸不对会直接报错或输出无意义。
7. **编码规范边界**：Google 规范只管 `src/`；`third_party/` 的开源库保持原样，配 `.clang-format` 时务必排除，避免大规模无意义 diff。
8. **MSVC 中文**：统一 UTF-8 + `/utf-8` 编译选项，否则中文日志/字符串易乱码。
