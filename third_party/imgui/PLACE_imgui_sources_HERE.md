# Dear ImGui

把 Dear ImGui 源码放到本目录，期望布局（CMake 据此查找）:

```
third_party/imgui/
├── imgui.cpp  imgui.h  imgui_draw.cpp  imgui_tables.cpp  imgui_widgets.cpp
├── imgui_internal.h  imconfig.h  imstb_*.h
└── backends/
    ├── imgui_impl_glfw.cpp   imgui_impl_glfw.h
    └── imgui_impl_opengl3.cpp imgui_impl_opengl3.h
```

- 来源: https://github.com/ocornut/imgui （取 `docking` 或 `master` tag 的源码）。
- 许可证: MIT（保持原样，勿改格式）。
- 依赖 GLFW + OpenGL3：GLFW 通过 vcpkg `glfw3` 提供（`find_package(glfw3)`）。

源码就位后 CMake 会构建 `imgui` 静态库并定义 `VOICE_HAVE_IMGUI=1`，
`src/ui.cc` 切换到真实 ImGui 界面；否则使用 headless 控制台 stub。
