// AC-1 文本闭环验收测试 (PRD §13)。
//
// 不依赖 GUI: 直接驱动 ChatController 的文本路径，验证发送文本后消息区出现
//   Me: <text>  和  Echo: <text>
// 走的是与 UI 完全相同的 ChatController -> LoopbackTransport -> on_text 链路。
#include <chrono>
#include <cstdio>
#include <thread>

#include "chat_controller.h"

int main() {
  using namespace std::chrono_literals;
  voice::ChatController controller;
  if (!controller.Init()) {
    std::printf("FAIL: ChatController.Init() returned false\n");
    return 1;
  }
  // 关闭模拟延迟，让回环尽快返回，使测试确定且快速。
  controller.settings().loopback_delay_ms.store(0);

  const std::string kText = "hello voice client";
  controller.OnTextSubmit(kText);

  bool saw_me = false;
  bool saw_echo = false;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline && !(saw_me && saw_echo)) {
    for (const auto& m : controller.log().Drain()) {
      if (m.kind == voice::MessageKind::kMe && m.text == kText) saw_me = true;
      if (m.kind == voice::MessageKind::kEcho && m.text == kText) saw_echo = true;
    }
    std::this_thread::sleep_for(10ms);
  }
  controller.Shutdown();

  if (saw_me && saw_echo) {
    std::printf("PASS: AC-1 text loopback (Me + Echo observed)\n");
    return 0;
  }
  std::printf("FAIL: AC-1 (saw_me=%d saw_echo=%d)\n", saw_me, saw_echo);
  return 1;
}
