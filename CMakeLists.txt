# ============================================================================
# Voice Client - 语音/文本客户端闭环测试
# Windows / VS2019 (MSVC v142) / x64 / C++17
#
# 生成 VS2019 解决方案:
#   cmake -G "Visual Studio 16 2019" -A x64 -B build .
#   cmake --build build --config Release
#
# 依赖策略:
#   - 轻量依赖 (GLFW / Dear ImGui / miniaudio) 通过 FetchContent 或源码内嵌。
#   - 重量依赖 (opus / webrtc-audio-processing / onnxruntime) 通过 vcpkg
#     提供 (find_package)，并用 CMake 选项控制，默认关闭以便阶段一先跑通。
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(voice_client
    VERSION 0.1.0
    DESCRIPTION "Voice/Text client loopback test (capture->process->codec->loopback->playback)"
    LANGUAGES CXX)

# ----------------------------------------------------------------------------
# 全局编译设置
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可执行文件与依赖 DLL 输出到同一目录，便于运行 (onnxruntime.dll 等需同目录)。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ----------------------------------------------------------------------------
# 功能开关 (阶段一默认走 stub / 直通，逐里程碑打开)
# ----------------------------------------------------------------------------
option(VOICE_ENABLE_OPUS    "Enable libopus codec (otherwise PCM passthrough)" OFF)
option(VOICE_ENABLE_WEBRTC  "Enable WebRTC APM (otherwise passthrough APM)"    OFF)
option(VOICE_ENABLE_SILERO  "Enable Silero VAD via ONNX Runtime"               OFF)
option(VOICE_ENABLE_TTS     "Enable Windows SAPI5 text-to-speech (/say)"       OFF)
option(VOICE_BUILD_TESTS    "Build test executables (AC verification)"         ON)

# ----------------------------------------------------------------------------
# MSVC 编译选项: /std:c++17 /utf-8 /W4  (third_party 降低告警级别, 见下方)
# ----------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
    # 让 __cplusplus 反映真实标准 (MSVC 默认报告 199711L)。
    add_compile_options(/Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ----------------------------------------------------------------------------
# 子目录
# ----------------------------------------------------------------------------
add_subdirectory(third_party)
add_subdirectory(src)

if(VOICE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
