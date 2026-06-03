#include "ui.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "audio_constants.h"

#if defined(VOICE_HAVE_IMGUI)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#endif

namespace voice {

namespace {

const char* KindPrefix(MessageKind k) {
  switch (k) {
    case MessageKind::kMe: return "Me: ";
    case MessageKind::kEcho: return "Echo: ";
    case MessageKind::kVoice: return "Voice: ";
    case MessageKind::kLog: return "[log] ";
  }
  return "";
}

}  // namespace

#if defined(VOICE_HAVE_IMGUI)

int RunUi(ChatController* controller) {
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  GLFWwindow* window =
      glfwCreateWindow(720, 600, "Voice Client - Loopback Test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  // 加载含中文字形的系统字体，否则 ImGui 默认字体只有 ASCII，中文显示为方块。
  // 字形范围用"简体常用"以控制字体纹理大小。
  {
    ImGuiIO& io = ImGui::GetIO();
    const char* kCjkFonts[] = {"C:\\Windows\\Fonts\\msyh.ttc",
                               "C:\\Windows\\Fonts\\simhei.ttf",
                               "C:\\Windows\\Fonts\\simsun.ttc"};
    for (const char* path : kCjkFonts) {
      std::ifstream f(path);
      if (!f.good()) continue;
      if (io.Fonts->AddFontFromFileTTF(
              path, 18.0f, nullptr,
              io.Fonts->GetGlyphRangesChineseSimplifiedCommon())) {
        break;  // 成功加载一种即可。
      }
    }
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  std::vector<std::string> history;  // 消息区累计行。
  char input_buf[1024] = {0};

  auto& ui = controller->ui_state();
  auto& cfg = controller->settings();

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    for (const auto& m : controller->log().Drain()) {
      history.push_back(std::string(KindPrefix(m.kind)) + m.text);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("VoiceClient", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // 1) 消息/日志区。
    ImGui::BeginChild("messages", ImVec2(0, -120), true);
    for (const auto& line : history) ImGui::TextWrapped("%s", line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
      ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // 2) 文本输入行 (回车或按钮发送)。
    bool submit = ImGui::InputText("##input", input_buf, sizeof(input_buf),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Send") || submit) {
      controller->OnTextSubmit(input_buf);
      input_buf[0] = '\0';
    }

    // 3) 语音控制行。
    bool mic = ui.mic_on.load();
    if (ImGui::Button(mic ? "闭麦 (Mic OFF)" : "开麦 (Mic ON)")) {
      controller->OnMicToggle(!mic);
    }
    ImGui::SameLine();
    ImGui::ProgressBar(ui.input_level.load(), ImVec2(120, 0), "level");
    ImGui::SameLine();
    ImGui::TextColored(ui.speaking.load() ? ImVec4(0, 1, 0, 1)
                                          : ImVec4(0.5f, 0.5f, 0.5f, 1),
                       "VAD");
    ImGui::SameLine();
    ImGui::TextColored(ui.playing.load() ? ImVec4(0, 0.4f, 1, 1)
                                         : ImVec4(0.5f, 0.5f, 0.5f, 1),
                       "PLAY");

    // 4) 设置区 (可折叠)。
    if (ImGui::CollapsingHeader("Settings")) {
      float thr = cfg.vad_threshold.load();
      if (ImGui::SliderFloat("VAD threshold", &thr, 0.0f, 1.0f))
        cfg.vad_threshold.store(thr);
      int sil = cfg.min_silence_ms.load();
      if (ImGui::SliderInt("End silence (ms)", &sil, kVadMinSilenceMsRange[0],
                           kVadMinSilenceMsRange[1]))
        cfg.min_silence_ms.store(sil);
      bool aec = cfg.aec_enabled.load();
      if (ImGui::Checkbox("AEC", &aec)) cfg.aec_enabled.store(aec);
      ImGui::SameLine();
      bool ns = cfg.ns_enabled.load();
      if (ImGui::Checkbox("NS", &ns)) cfg.ns_enabled.store(ns);
      int delay = cfg.loopback_delay_ms.load();
      if (ImGui::SliderInt("Loopback delay (ms)", &delay, 0, 1000))
        cfg.loopback_delay_ms.store(delay);
      bool rt = cfg.realtime_monitor.load();
      if (ImGui::Checkbox("Realtime Monitor (skip VAD)", &rt))
        cfg.realtime_monitor.store(rt);
    }

    ImGui::End();
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

#else  // !VOICE_HAVE_IMGUI —— headless 控制台 stub。

// 简易交互: 直接输入文本回车发送; 命令 /mic 切换开麦, /quit 退出。
// 便于阶段一在未集成 ImGui 时验证文本闭环 (AC-1) 与链路。
int RunUi(ChatController* controller) {
  std::printf(
      "[headless] Dear ImGui 未集成。输入文本回车发送, /mic 开关麦, /quit 退出。\n");
  std::string line;
  bool running = true;
  while (running) {
    for (const auto& m : controller->log().Drain())
      std::printf("%s%s\n", KindPrefix(m.kind), m.text.c_str());
    std::printf("> ");
    std::fflush(stdout);
    if (!std::getline(std::cin, line)) break;
    if (line == "/quit") {
      running = false;
    } else if (line == "/mic") {
      controller->OnMicToggle(!controller->ui_state().mic_on.load());
    } else if (!line.empty()) {
      controller->OnTextSubmit(line);
    }
  }
  return 0;
}

#endif

}  // namespace voice
