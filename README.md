# Voice Client — 语音/文本客户端闭环测试

用 C++17 实现的桌面测试程序，验证「持续监听语音 + 文本输入」交互链路的**客户端**部分：

```
麦克风 ─► 采集 ─► AEC/降噪(APM) ─► VAD 端点检测 ─► Opus 编码 ─► [Loopback 发送]
                                                                      │ 延迟回传
扬声器 ◄─ 播放 ◄─ jitter buffer ◄─ Opus 解码 ◄──────────────────────┘
```

不接入真实 AIServer/WebSocket，用本地 **Loopback** 模块把发出去的音频/文本原样（或带延迟地）还回来，从而在没有后端的情况下完成端到端验证。需求详见 [`voice_client_prd.md`](voice_client_prd.md)。

> 客户端**不含任何大模型**；唯一可选的模型文件是 Silero VAD（约 1~2MB，仅判断「是否有人说话」）。默认走纯能量 VAD，零模型文件即可运行。

## 开发环境

| 项 | 要求 |
|---|---|
| OS | Windows 10/11 x64 |
| 编译器 | Visual Studio 2019（MSVC v142） |
| C++ | C++17（`/std:c++17 /utf-8 /W4`） |
| 构建 | CMake ≥ 3.20 |

## 构建

```powershell
cmake -G "Visual Studio 16 2019" -A x64 -B build .
cmake --build build --config Release
# 产物: build/bin/Release/voice_client.exe
```

### CMake Presets（推荐，一键切配置）

`CMakePresets.json` 预置了各里程碑配置（CMake ≥ 3.21）：

```powershell
cmake --list-presets                       # 列出全部
cmake --preset all                          # 配置: 全功能 (Opus+Silero+WebRTC+TTS+GUI)
cmake --build --preset all                  # 构建
ctest --preset all                          # 跑验收用例
```

| preset | 内容 | 备注 |
|---|---|---|
| `default` | 纯净（能量 VAD + 直通 APM + GUI） | 阶段一基线，无重依赖 |
| `opus` | + Opus | |
| `silero` | + Opus + Silero VAD | 需 ONNX Runtime（自动下载）+ 模型 |
| `webrtc` | + Opus + WebRTC APM | 需 `third_party/webrtc-apm/` 预编译产物 |
| `tts` | + Opus + SAPI5 TTS | Windows 自带语音，无额外依赖/无模型 |
| `all` | 全功能 | 完整产品 |
| `ci-headless` | Ninja 无 GUI，仅测试 | 在 VS 开发者命令行中运行，供 CI |

### 功能开关（按里程碑逐步打开）

阶段一默认全部 **OFF**，走 stub/直通，先把框架与文本闭环跑通：

| 选项 | 默认 | 作用 |
|---|---|---|
| `VOICE_ENABLE_OPUS` | OFF | 启用 libopus（关闭时 Loopback 传 PCM 直通） |
| `VOICE_ENABLE_WEBRTC` | OFF | 启用 WebRTC APM（关闭时用直通 APM，**务必戴耳机**） |
| `VOICE_ENABLE_SILERO` | OFF | 启用 Silero VAD（关闭时用能量 VAD 占位） |
| `VOICE_ENABLE_TTS` | OFF | 启用 Windows SAPI5 文字转语音（`/say` 命令；关闭/非 Windows 时为 stub） |

```powershell
cmake -G "Visual Studio 16 2019" -A x64 -B build -DVOICE_ENABLE_OPUS=ON .
```

## 完整版本构建（实测步骤）

预置 preset 把生成器写死成 `Visual Studio 16 2019`；**没装 VS2019** 时（例如只有
VS2022 / VS 17/18 或 Build Tools），用下面这条 **Ninja + vcvars** 的路子，全功能且
可用 `/say`。

### 0) 前置

- 任意带「C++ 桌面开发」工作负载的 Visual Studio（含 MSVC + Windows SDK；自带 Ninja）。
- CMake ≥ 3.20；**联网**（Opus / ONNX Runtime / ImGui / GLFW 由 CMake `FetchContent`
  自动下载源码构建，无需 vcpkg）。

### 1) 准备被 `.gitignore` 排除的大文件（全功能必需）

全新 clone 不含模型与 WebRTC 预编译库，需先获取：

```powershell
# Silero VAD 模型 -> models/silero_vad.onnx
#   从 https://github.com/snakers4/silero-vad 取 silero_vad.onnx (v5, 512 样本@16k)
#   放到 models/ 下 (CMake 会在构建时自动拷到 exe 同级 models/)

# WebRTC APM 预编译库 (~50MB)
powershell -ExecutionPolicy Bypass -File third_party\webrtc-apm\fetch_webrtc_apm.ps1
# 成功后应存在 third_party\webrtc-apm\extracted\lib\audio_processing.lib
```

### 2) 进 MSVC 环境并配置 + 构建（全功能 + headless）

在 **「x64 Native Tools Command Prompt for VS」** 里，或先 `call` 一次 vcvars：

```powershell
# 例: call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -G Ninja -B build-all -DCMAKE_BUILD_TYPE=Release `
    -DVOICE_ENABLE_OPUS=ON -DVOICE_ENABLE_SILERO=ON `
    -DVOICE_ENABLE_WEBRTC=ON -DVOICE_ENABLE_TTS=ON `
    -DVOICE_ENABLE_IMGUI=OFF
cmake --build build-all
```

> `-DVOICE_ENABLE_IMGUI=OFF` 是关键：`/say` 是 **headless 控制台命令**，GUI 路径里
> 没有它，且无显示环境会卡在开窗。要图形界面就去掉这行（但当前 GUI 暂无 `/say` 按钮）。
> 若你**有 VS2019**，等价的一键写法是 `cmake --preset all` / `cmake --build --preset all`。

### 3) 运行与自测

```powershell
.\build-all\bin\voice_client.exe        # onnxruntime.dll 与 models\ 已随构建自动部署
ctest --test-dir build-all --output-on-failure
```

控制台命令：`/say <文字>` 朗读、直接打字回车=文本回显、`/mic` 开关麦、`/quit` 退出。

> ⚠️ **务必戴耳机**测试 `/mic`：外放时扬声器回放会被麦克风重新采到，灵敏的 Silero
> 会当成插话触发 barge-in 把回放掐断（表现为"听不到回放"）。

排障小工具（手动运行，不入 ctest）：`build-all\bin\mic_level_monitor.exe`（看麦克风
电平）、`build-all\bin\vad_monitor.exe [passthrough]`（实时看 VAD 概率与起止事件）。

## 依赖获取

| 库 | 集成方式 | 放置/安装 |
|---|---|---|
| miniaudio | 源码内嵌（单头） | `third_party/miniaudio/miniaudio.h`（见该目录 README） |
| Dear ImGui | 源码内嵌 | `third_party/imgui/`（见该目录 README）；缺失则 UI 降级为控制台 stub |
| GLFW | vcpkg | `vcpkg install glfw3:x64-windows` |
| libopus | vcpkg | `vcpkg install opus:x64-windows` |
| onnxruntime | vcpkg / 官方预编译 | `vcpkg install onnxruntime:x64-windows`，DLL 随 exe 部署 |
| webrtc-audio-processing | 预编译产物 | 已从 vcpkg 默认 registry 移除；运行 `third_party/webrtc-apm/fetch_webrtc_apm.ps1` 获取 M124 win-x64 预编译 `audio_processing.lib`（CMake 自动检测） |

使用 vcpkg 时传 `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`。

> **依赖难度**：WebRTC APM 最难编译，故默认 OFF 用直通 APM 先跑通（PRD §3 注）。

## 当前实现状态

脚手架已就位且**无第三方库时即可编译**（UI 走 headless 控制台、采集/播放 Start() 返回 false）。已完整实现且不依赖三方库的部分：

- ✅ Loopback 传输（延迟回传 + barge-in 取消，`transport.cc`）
- ✅ SPSC 无锁环形缓冲（`ring_buffer.h`）
- ✅ 直通 APM、PCM 直通 codec、能量 VAD 端点状态机
- ✅ ChatController 处理线程 + 帧长重切对齐 + barge-in 逻辑
- ✅ jitter buffer 播放队列 + AEC render 参考帧对齐
- ✅ **M1 文本闭环**（AC-1）：`ctest` 用例 `ac1_text_loopback`
- ✅ **M2 miniaudio 采集/播放 + Realtime Monitor**（§7.2、AC-6 基础）：
  `m2_monitor_loopback`（合成 PCM 全链路往返）、`m2_device_probe`（声卡存活性）
- ✅ **M3 接入 Opus**（AC-5）：`-DVOICE_ENABLE_OPUS=ON`（FetchContent 构建
  libopus），`ac5_opus_codec` 验证编解码完整性与压缩
- ✅ **M4 端点检测 + Utterance Loopback + Silero**（AC-3/AC-4）：
  `ac3_endpoint`、`ac4_utterance_loopback`（能量 VAD，确定性）；
  `-DVOICE_ENABLE_SILERO=ON` 接入真实 Silero VAD（ONNX Runtime 自动下载、
  `onnxruntime.dll` 随 exe 部署），`m4_silero_smoke` 验证推理
- ✅ **M5 Barge-in**（AC-7）：播放中检测到说话立即 `Flush()` + 取消待回传 +
  日志 `barge-in: playback flushed`；`ac7_bargein` 验证机制 + ChatController 集成
- ✅ **M6 WebRTC APM**（AC-8）：`WebrtcApm`（AEC3+高强度 NS+自适应 AGC+高通，
  10ms 帧、render/capture 对齐），`-DVOICE_ENABLE_WEBRTC=ON` 启用，ChatController
  自动切换。用 M124 预编译产物（见 `third_party/webrtc-apm/`）链入 MSVC，避开
  Meson/clang 构建难题；`ac8_aec` 实测回授残余 **~0.7%（≈ −43dB）**。
- ✅ **文字转语音（`/say`）**：`-DVOICE_ENABLE_TTS=ON`（或 `--preset tts`）启用
  Windows SAPI5。控制台输入 `/say <文字>`：文本→SAPI 合成 16k/mono/int16 PCM→
  走下行（Opus/直通编码→Loopback 延迟回传→解码→jitter→播放）。无额外依赖/无模型；
  中文需系统装中文语音包。`tts_synthesize_smoke` 验证合成返回非空 PCM。
- ✅ **M7 打磨**（AC-9/AC-10）：参数实时下发（含闭麦下文本路径的 Loopback 延迟），
  `ac9_param_realtime` 证明改句末静音阈值即改变断句；`src/` 通过规范自查
  （≤100 列、无 Tab、无尾空白、统一 `#pragma once`）。

### 验收矩阵（PRD §13）

| 用例 | 状态 | 验证方式 |
|---|---|---|
| AC-1 文本闭环 | ✅ | `ac1_text_loopback` |
| AC-2 持续监听 | ✅ 管线 / 🟡 主观 | `m2_device_probe`（采集链路存活）；电平随声起伏需真机目测 |
| AC-3 端点检测 | ✅ | `ac3_endpoint` |
| AC-4 语音闭环 | ✅ | `ac4_utterance_loopback`（真实 ChatController） |
| AC-5 编解码完整 | ✅ | `ac5_opus_codec`（样本守恒 + 压缩） |
| AC-6 播放平滑 | ✅ 数据 / 🟡 主观 | `m2_monitor_loopback`（jitter 正确）；听感需真机 |
| AC-7 Barge-in | ✅ | `ac7_bargein`（机制 + 集成） |
| AC-8 AEC | ✅ | `ac8_aec`（真实 WebRTC APM，回授残余 ~0.7%） |
| AC-9 参数实时 | ✅ | `ac9_param_realtime` |
| AC-10 工程/规范 | ✅ 规范 / 🟡 VS2019 | 规范自查通过、third_party 未改格式；VS2019 生成器需在装有 VS2019 的机器复验 |

里程碑 M1~M7 见 PRD §14。测试用 `ctest --test-dir <build>` 运行。

## 目录结构

```
voice-client/
├── CMakeLists.txt          # 根: 工具链/编译选项/功能开关
├── .clang-format           # Google 风格，仅作用于 src/
├── .editorconfig
├── docs/CODING_GUIDELINES.md  # 编码规范与开发约束
├── CLAUDE.md               # 给 AI 编码的项目约束
├── third_party/            # 开源库（保持原风格，.clang-format DisableFormat）
├── models/                 # silero_vad.onnx（按需）
└── src/                    # 本项目自有代码（遵循 Google 规范）
    ├── types.h  audio_constants.h  ring_buffer.h  message_log.h
    ├── audio_capture.{h,cc}   audio_processor.{h,cc}
    ├── vad.{h,cc}             codec.{h,cc}
    ├── transport.{h,cc}       audio_playback.{h,cc}
    ├── chat_controller.{h,cc} ui.{h,cc}  main.cc
    └── CMakeLists.txt
```

## 编码规范

本项目自有代码遵循 **Google C++ Style Guide**；第三方库保持原样。详见
[`docs/CODING_GUIDELINES.md`](docs/CODING_GUIDELINES.md)。
