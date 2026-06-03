// UI (PRD §5) —— Dear ImGui + GLFW + OpenGL3，单窗口固定布局。
//
// 未集成 ImGui 源码时 (VOICE_HAVE_IMGUI 未定义)，编译为 headless 控制台
// stub，便于阶段一先验证非 UI 链路。
#pragma once

#include "chat_controller.h"

namespace voice {

// 创建窗口并进入主循环，直到用户关闭窗口后返回。
// controller 必须已 Init()。所有 UI 交互通过 controller 的方法/状态完成。
int RunUi(ChatController* controller);

}  // namespace voice
