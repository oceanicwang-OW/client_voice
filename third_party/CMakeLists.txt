# ============================================================================
# 第三方依赖
#
# 原则 (PRD §2.2): 开源库保持原有代码风格，原样纳入；不套用 Google 规范。
# 本目录 .clang-format 设 DisableFormat: true，防止误格式化 FetchContent 拉取
# 或内嵌的源码。
#
# 集成方式:
#   - miniaudio : 单头文件，源码内嵌 (third_party/miniaudio/miniaudio.h)。
#   - GLFW+ImGui: VOICE_ENABLE_IMGUI=ON 时 FetchContent 拉取构建 (默认)；
#                 OFF 时 UI 退化为 headless 控制台 (便于无 GUI 环境/测试)。
#   - opus / webrtc-audio-processing / onnxruntime: vcpkg find_package，
#     仅在对应 VOICE_ENABLE_* 选项打开时引入。
# ============================================================================
include(FetchContent)

option(VOICE_ENABLE_IMGUI "Build Dear ImGui GUI (else headless console)" ON)

# --- miniaudio (header-only, 内嵌) ------------------------------------------
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio")

# --- GLFW + Dear ImGui ------------------------------------------------------
if(VOICE_ENABLE_IMGUI)
    set(IMGUI_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

    # GLFW: 优先 vcpkg/系统包，否则 FetchContent 源码构建。
    find_package(glfw3 CONFIG QUIET)
    if(NOT glfw3_FOUND)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(glfw)
    endif()

    find_package(OpenGL REQUIRED)

    # ImGui: 优先内嵌源码 (third_party/imgui/)，否则 FetchContent 拉取。
    if(EXISTS "${IMGUI_LOCAL}/imgui.cpp")
        set(IMGUI_DIR "${IMGUI_LOCAL}")
    else()
        FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.91.5
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(imgui)  # 无 CMakeLists, 仅 populate。
        set(IMGUI_DIR "${imgui_SOURCE_DIR}")
    endif()

    add_library(imgui STATIC
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(imgui PUBLIC
        "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    set(VOICE_HAVE_IMGUI TRUE PARENT_SCOPE)
else()
    message(STATUS "[third_party] VOICE_ENABLE_IMGUI=OFF; UI builds as "
                   "headless console stub.")
endif()

# --- opus (vcpkg 优先, 否则 FetchContent 源码构建) --------------------------
if(VOICE_ENABLE_OPUS)
    find_package(Opus CONFIG QUIET)
    if(NOT Opus_FOUND)
        set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(opus
            GIT_REPOSITORY https://github.com/xiph/opus.git
            GIT_TAG v1.5.2
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(opus)
        # 老/新版本目标名兼容: 确保存在 Opus::opus 别名供 src 链接。
        if(TARGET opus AND NOT TARGET Opus::opus)
            add_library(Opus::opus ALIAS opus)
        endif()
    endif()
endif()

# --- onnxruntime (Silero VAD) ----------------------------------------------
# vcpkg/官方 CMake 包优先; 否则下载官方 Windows x64 预编译包并建导入目标。
if(VOICE_ENABLE_SILERO)
    find_package(onnxruntime CONFIG QUIET)
    if(NOT onnxruntime_FOUND)
        set(ORT_VER 1.17.3)
        FetchContent_Declare(onnxruntime
            URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VER}/onnxruntime-win-x64-${ORT_VER}.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(onnxruntime)
        # 兼容压缩包可能保留顶层目录的情况。
        if(EXISTS "${onnxruntime_SOURCE_DIR}/include/onnxruntime_cxx_api.h")
            set(ORT_ROOT "${onnxruntime_SOURCE_DIR}")
        else()
            file(GLOB _ort_sub LIST_DIRECTORIES true
                 "${onnxruntime_SOURCE_DIR}/onnxruntime-win-x64-*")
            list(GET _ort_sub 0 ORT_ROOT)
        endif()
        add_library(onnxruntime SHARED IMPORTED GLOBAL)
        set_target_properties(onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ORT_ROOT}/lib/onnxruntime.dll"
            IMPORTED_IMPLIB "${ORT_ROOT}/lib/onnxruntime.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include")
        if(NOT TARGET onnxruntime::onnxruntime)
            add_library(onnxruntime::onnxruntime ALIAS onnxruntime)
        endif()
    endif()
endif()

# --- webrtc-audio-processing (AEC3/NS/AGC) ----------------------------------
# 优先用本地预编译产物 (third_party/webrtc-apm/extracted: 来自
# get-wrecked/webrtc-audioprocessing 的 M124 win-x64 预编译, 单个 audio_
# processing.lib 已含 abseil 等依赖)。否则回退 vcpkg find_package。
# 统一暴露 INTERFACE/IMPORTED 目标 webrtc_apm 供 src 链接。
if(VOICE_ENABLE_WEBRTC)
    set(WEBRTC_PREBUILT "${CMAKE_CURRENT_SOURCE_DIR}/webrtc-apm/extracted")
    if(EXISTS "${WEBRTC_PREBUILT}/lib/audio_processing.lib")
        add_library(webrtc_apm STATIC IMPORTED GLOBAL)
        set_target_properties(webrtc_apm PROPERTIES
            IMPORTED_LOCATION "${WEBRTC_PREBUILT}/lib/audio_processing.lib"
            INTERFACE_INCLUDE_DIRECTORIES
                "${WEBRTC_PREBUILT}/src;${WEBRTC_PREBUILT}/src/third_party/abseil-cpp"
            INTERFACE_COMPILE_DEFINITIONS "WEBRTC_WIN;NOMINMAX")
        if(WIN32)
            set_property(TARGET webrtc_apm APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES winmm)
        endif()
        set(VOICE_HAVE_WEBRTC TRUE PARENT_SCOPE)
        message(STATUS "[third_party] WebRTC APM: using prebuilt ${WEBRTC_PREBUILT}/lib")
    else()
        find_package(unofficial-webrtc-audio-processing CONFIG QUIET)
        if(unofficial-webrtc-audio-processing_FOUND)
            add_library(webrtc_apm INTERFACE)
            target_link_libraries(webrtc_apm INTERFACE
                unofficial::webrtc-audio-processing::webrtc-audio-processing)
            set(VOICE_HAVE_WEBRTC TRUE PARENT_SCOPE)
        else()
            message(WARNING "[third_party] webrtc-audio-processing not found; "
                            "falling back to passthrough APM at runtime.")
        endif()
    endif()
endif()
