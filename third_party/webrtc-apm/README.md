# WebRTC APM (AEC3 / NS / AGC) — M6

本项目用 [get-wrecked/webrtc-audioprocessing](https://github.com/get-wrecked/webrtc-audioprocessing)
的 **M124 预编译产物** 提供 WebRTC 音频处理模块：单个 `audio_processing.lib`
已静态包含 abseil 等依赖，可直接链接进 MSVC x64 工程，避开 `webrtc-audio-processing`
的 Meson/clang 构建难题（PRD §3，且该库已从 vcpkg 默认 registry 移除）。

## 获取（不入库，~50MB）

```powershell
powershell -ExecutionPolicy Bypass -File .\fetch_webrtc_apm.ps1
```

完成后目录布局：

```
third_party/webrtc-apm/extracted/
├── lib/audio_processing.lib                       # 静态库 (含 abseil)
└── src/                                           # 头文件根 (include dir)
    ├── modules/audio_processing/include/audio_processing.h
    ├── api/...   rtc_base/...
    └── third_party/abseil-cpp/absl/...            # abseil 头 (额外 include dir)
```

## 启用

```powershell
cmake -G "Visual Studio 16 2019" -A x64 -DVOICE_ENABLE_WEBRTC=ON -B build .
```

`third_party/CMakeLists.txt` 检测到 `extracted/lib/audio_processing.lib` 后建
导入目标 `webrtc_apm`（include = `src` + `src/third_party/abseil-cpp`，定义
`WEBRTC_WIN`/`NOMINMAX`，链接 `winmm`）。`audio_processor.cc` 的 `WebrtcApm`
适配器（AEC3 + 高强度 NS + 自适应 AGC + 高通）随之编入，`ChatController` 自动
从直通切到真实 APM。

验证：`ctest -R ac8_aec`（回授衰减，实测残余 ~0.7% ≈ −43dB）。

## 替代方案

若需自行编译或换平台：见上游仓库（需 depot_tools + `gn` + 完整 WebRTC 源码），
或为 `webrtc-audio-processing`（pulseaudio standalone, Meson）写 vcpkg overlay
port；二者在 MSVC 上都更繁琐，故本项目优先用预编译产物。
