# AGENTS.md — 项目约束（供 AI 编码遵循）

本仓库是「语音/文本客户端闭环测试」。完整需求见 `voice_client_prd.md`，编码规范见
`docs/CODING_GUIDELINES.md`。开始任何改动前先读这两份。

## 项目本质

- C++17 桌面程序，验证客户端音频链路：采集→APM→VAD→Opus→**Loopback**→解码→播放。
- **不接入真实后端**：用 `LoopbackTransport` 原样/延迟回传替代 WebSocket+AIServer。
- 客户端不含大模型；Silero VAD 仅判断「是否有人说话」，且默认用能量 VAD 占位。

## 硬性约束（违反即视为 bug）

1. 编码规范：自有代码（`src/`）遵循 Google C++ Style；`third_party/` 一律不动、不格式化。
2. 全链路 PCM 统一 **16kHz / 单声道 / int16**。音频常量集中在 `src/audio_constants.h`，勿写魔数。
3. 帧长不同，模块间必须重新切帧对齐：采集→APM(160)→VAD(512)→Opus(320)。
4. 实时回调（采集/播放）禁阻塞、禁分配大内存、禁打日志、禁加锁等待。
5. 跨线程：采集/播放↔处理用 SPSC 环形缓冲；UI↔处理用 `std::atomic` + `MessageLog`。
6. Barge-in 路径要快：`Flush()` + `CancelPendingAudio()` + VAD `Reset()`。
7. 中文不乱码：源码 UTF-8 + 编译 `/utf-8`。

## 功能开关（CMake）

阶段一默认全 OFF，走 stub/直通。逐里程碑打开，勿一次性全开：

- `VOICE_ENABLE_OPUS` / `VOICE_ENABLE_WEBRTC` / `VOICE_ENABLE_SILERO`
- 关闭时分别用：PCM 直通 / 直通 APM / 能量 VAD。代码用同名宏切换，保持接口不变。

## 里程碑顺序（PRD §14，建议按此推进）

1. **M1** 框架 + 文本闭环（CMake/UI/LoopbackTransport 文本）→ AC-1
2. **M2** miniaudio 采集→直通→播放（Realtime Monitor）→ AC-2/§7.2
3. **M3** 接入 Opus（`VOICE_ENABLE_OPUS`）→ AC-5
4. **M4** 接入 Silero VAD + 断句（`VOICE_ENABLE_SILERO`）→ AC-3/AC-4
5. **M5** Barge-in → AC-7
6. **M6** 接入 WebRTC APM（`VOICE_ENABLE_WEBRTC`）+ render 对齐 → AC-8
7. **M7** 打磨：参数面板、jitter 调优、日志、规范自查 → AC-9/AC-10

## 接口契约

- 替换实现时保持接口不变：`ITransport`（Loopback↔WebSocket）、`IAudioProcessor`
  （passthrough↔WebRTC）、`VadDetector`（energy↔Silero）。新增实现勿改调用方。

## 验证

- 无任何第三方库时项目也应能编译（UI=headless，采集/播放 Start() 返回 false）。
- 修改后至少确认目标能配置+编译；涉及音频逻辑时对照 PRD §13 验收用例自查。
- 戴耳机测试音频闭环，避免外放自激（尤其 AEC 未接入时）。
