# 测试 (链接 voice_core，不依赖 GUI)。

# M1: 文本闭环 (AC-1)。
add_executable(text_loopback_test text_loopback_test.cc)
target_link_libraries(text_loopback_test PRIVATE voice_core)
add_test(NAME ac1_text_loopback COMMAND text_loopback_test)

# M2: Realtime Monitor 数据通路 (AC-6 基础, §7.2)。无硬件，确定性。
add_executable(m2_monitor_loopback_test m2_monitor_loopback_test.cc)
target_link_libraries(m2_monitor_loopback_test PRIVATE voice_core)
add_test(NAME m2_monitor_loopback COMMAND m2_monitor_loopback_test)

# M2: 音频设备探针 (AC-2 链路存活性)。依赖声卡，无设备则 SKIP。
add_executable(audio_device_probe audio_device_probe.cc)
target_link_libraries(audio_device_probe PRIVATE voice_core)
add_test(NAME m2_device_probe COMMAND audio_device_probe)

# 麦克风电平实时监视 (手动诊断, 不注册为 ctest 用例)。
add_executable(mic_level_monitor mic_level_monitor.cc)
target_link_libraries(mic_level_monitor PRIVATE voice_core)

# 实时 VAD 探针 (手动诊断, 不注册为 ctest 用例)。模型路径编进去, 不依赖 CWD。
add_executable(vad_monitor vad_monitor.cc)
target_link_libraries(vad_monitor PRIVATE voice_core)
target_compile_definitions(vad_monitor PRIVATE
    VOICE_SILERO_MODEL_PATH="${CMAKE_SOURCE_DIR}/models/silero_vad.onnx")

# M3: Opus 编解码完整性 (AC-5)。-DVOICE_ENABLE_OPUS=ON 时走真实 Opus。
add_executable(ac5_opus_codec_test ac5_opus_codec_test.cc)
target_link_libraries(ac5_opus_codec_test PRIVATE voice_core)
add_test(NAME ac5_opus_codec COMMAND ac5_opus_codec_test)

# 后端无关的逻辑测试 (端点/闭环/打断/参数) 用合成正弦 + 能量 VAD + 直通 APM
# 做确定性验证。真实 Silero (合成信号未必判为语音) 或真实 WebRTC APM (高强度
# 降噪会把稳态正弦当噪声抑制) 会改变合成信号行为，故这些用例仅在"纯净"配置
# (能量 VAD + 直通 APM) 注册; 对应里程碑逻辑与构建开关无关，已在该配置覆盖。
set(VOICE_PURE_BACKEND TRUE)
if(VOICE_ENABLE_SILERO OR VOICE_ENABLE_WEBRTC)
    set(VOICE_PURE_BACKEND FALSE)
endif()

# M4: 端点检测状态机 (AC-3) 与 Utterance Loopback (AC-4)。
add_executable(ac3_endpoint_test ac3_endpoint_test.cc)
target_link_libraries(ac3_endpoint_test PRIVATE voice_core)
add_executable(ac4_utterance_loopback_test ac4_utterance_loopback_test.cc)
target_link_libraries(ac4_utterance_loopback_test PRIVATE voice_core)
if(VOICE_PURE_BACKEND)
    add_test(NAME ac3_endpoint COMMAND ac3_endpoint_test)
    add_test(NAME ac4_utterance_loopback COMMAND ac4_utterance_loopback_test)
endif()

# M5: Barge-in (AC-7)。机制部分确定性; 集成部分确定性 (PumpPlaybackForTest)。
add_executable(ac7_bargein_test ac7_bargein_test.cc)
target_link_libraries(ac7_bargein_test PRIVATE voice_core)
if(VOICE_PURE_BACKEND)
    add_test(NAME ac7_bargein COMMAND ac7_bargein_test)
endif()

# M6: AEC (AC-8)。直通构建下为 plumbing 烟雾; -DVOICE_ENABLE_WEBRTC=ON 时
# 做回授衰减功能验证。
add_executable(ac8_aec_test ac8_aec_test.cc)
target_link_libraries(ac8_aec_test PRIVATE voice_core)
add_test(NAME ac8_aec COMMAND ac8_aec_test)

# M7: 参数实时生效 (AC-9)。改变句末静音阈值 -> 断句结果随之变化。
add_executable(ac9_param_realtime_test ac9_param_realtime_test.cc)
target_link_libraries(ac9_param_realtime_test PRIVATE voice_core)
if(VOICE_PURE_BACKEND)
    add_test(NAME ac9_param_realtime COMMAND ac9_param_realtime_test)
endif()

# TTS 合成烟雾测试 (仅 -DVOICE_ENABLE_TTS=ON)。合成英文 "hello" 断言返回
# 非空 PCM (英文音色几乎必装); 中文依赖系统中文语音包, 不在此断言。
if(VOICE_ENABLE_TTS)
    add_executable(tts_synthesize_smoke_test tts_synthesize_smoke_test.cc)
    target_link_libraries(tts_synthesize_smoke_test PRIVATE voice_core)
    add_test(NAME tts_synthesize_smoke COMMAND tts_synthesize_smoke_test)
endif()

# M4: Silero VAD 集成烟雾测试 (仅 -DVOICE_ENABLE_SILERO=ON)。
# 加载真实 silero_vad.onnx 并推理，校验返回合法概率、静音判为低概率。
if(VOICE_ENABLE_SILERO)
    add_executable(silero_vad_smoke_test silero_vad_smoke_test.cc)
    target_link_libraries(silero_vad_smoke_test PRIVATE voice_core)
    target_compile_definitions(silero_vad_smoke_test PRIVATE
        VOICE_SILERO_MODEL_PATH="${CMAKE_SOURCE_DIR}/models/silero_vad.onnx")
    add_custom_command(TARGET silero_vad_smoke_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:onnxruntime> $<TARGET_FILE_DIR:silero_vad_smoke_test>)
    add_test(NAME m4_silero_smoke COMMAND silero_vad_smoke_test)
endif()
