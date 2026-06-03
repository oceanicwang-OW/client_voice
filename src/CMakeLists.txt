# ============================================================================
# 本项目自有代码 (遵循 Google C++ Style Guide, 见 .clang-format)
#
# 拆分为:
#   voice_core  —— 链路逻辑静态库 (无 UI/无 main)，供 exe 与测试共用。
#   voice_client —— 可执行文件 (main + ui)。
# ============================================================================
add_library(voice_core STATIC
    audio_capture.cc
    audio_processor.cc
    vad.cc
    codec.cc
    transport.cc
    audio_playback.cc
    tts.cc
    chat_controller.cc)

target_include_directories(voice_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(voice_core PUBLIC miniaudio)

if(WIN32)
    target_link_libraries(voice_core PUBLIC winmm)
endif()

# 功能开关透传 (codec.cc / vad.cc / audio_processor.cc 用同名宏切换实现)。
if(VOICE_ENABLE_OPUS)
    target_link_libraries(voice_core PUBLIC Opus::opus)
    target_compile_definitions(voice_core PUBLIC VOICE_ENABLE_OPUS=1)
endif()
if(VOICE_ENABLE_SILERO)
    target_link_libraries(voice_core PUBLIC onnxruntime::onnxruntime)
    target_compile_definitions(voice_core PUBLIC VOICE_ENABLE_SILERO=1)
endif()
if(VOICE_ENABLE_WEBRTC AND VOICE_HAVE_WEBRTC)
    target_link_libraries(voice_core PUBLIC webrtc_apm)
    target_compile_definitions(voice_core PUBLIC VOICE_ENABLE_WEBRTC=1)
endif()
if(VOICE_ENABLE_TTS AND WIN32)
    # SAPI5 经 COM 调用; CLSID/IID 符号来自 sapi.lib, COM 运行时来自 ole32。
    target_link_libraries(voice_core PUBLIC sapi ole32)
    target_compile_definitions(voice_core PUBLIC VOICE_ENABLE_TTS=1)
endif()

# --- 可执行文件 -------------------------------------------------------------
add_executable(voice_client main.cc ui.cc)
target_link_libraries(voice_client PRIVATE voice_core)

if(VOICE_HAVE_IMGUI)
    target_link_libraries(voice_client PRIVATE imgui)
    target_compile_definitions(voice_client PRIVATE VOICE_HAVE_IMGUI=1)
endif()

# 部署 onnxruntime.dll 与 Silero 模型到可执行文件目录 (PRD §12)。
# 模型按相对路径 "models/silero_vad.onnx" 加载, 故拷到 exe 同级的 models/ 下,
# 使双击运行 (CWD=exe 目录) 也能找到模型, 而非仅从仓库根运行。
if(VOICE_ENABLE_SILERO)
    add_custom_command(TARGET voice_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:onnxruntime> $<TARGET_FILE_DIR:voice_client>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/models/silero_vad.onnx"
            "$<TARGET_FILE_DIR:voice_client>/models/silero_vad.onnx")
endif()
