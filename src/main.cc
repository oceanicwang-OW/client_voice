// 程序入口 (PRD §12)。创建 ChatController，进入 UI 主循环。
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "chat_controller.h"
#include "ui.h"

int main() {
#if defined(_WIN32)
  // 源码/字符串为 UTF-8 (/utf-8)。把控制台输出代码页设为 UTF-8，避免
  // headless 控制台把 UTF-8 字节按 GBK 解释而出现中文乱码。
  SetConsoleOutputCP(CP_UTF8);
#endif
  voice::ChatController controller;
  if (!controller.Init()) {
    std::fprintf(stderr, "ChatController 初始化失败\n");
    return 1;
  }
  const int rc = voice::RunUi(&controller);
  controller.Shutdown();
  return rc;
}
