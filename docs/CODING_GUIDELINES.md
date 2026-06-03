# 编码规范与开发约束

> 适用范围：**本项目自有代码**（`src/`，含对开源库的封装/适配层）。
> 第三方/开源库源码（`third_party/`）**保持原有风格，原样纳入**，不重排、不套用本规范，以便后续升级合并。

## 1. 总则

- 自有代码遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)。
- 语言标准：C++17。MSVC 编译选项：`/std:c++17 /utf-8 /W4 /permissive-`。
- 字符集统一 **UTF-8**（源码与字符串）；缺 `/utf-8` 会导致中文乱码（PRD §15.8）。
- 格式化由 `.clang-format`（`BasedOnStyle: Google`）统一执行，仅作用于 `src/`；
  `third_party/.clang-format` 设 `DisableFormat: true` 防止误格式化。

## 2. 命名

| 实体 | 约定 | 示例 |
|---|---|---|
| 类型/类/结构体/枚举 | `PascalCase` | `AudioCapture`, `VadEvent` |
| 函数/方法 | `PascalCase` | `ProcessCapture()`, `OnMicToggle()` |
| 变量/参数 | `snake_case` | `frame_count`, `sample_rate` |
| 类成员变量 | `snake_case_`（尾下划线） | `running_`, `input_level_` |
| 常量 / `constexpr` | `kConstantName` | `kSampleRate`, `kOpusFrameSamples` |
| 宏 | `UPPER_SNAKE_CASE` | `VOICE_ENABLE_OPUS` |
| 命名空间 | `snake_case` | `voice` |
| 文件 | `snake_case`，扩展名 `.cc` / `.h`（全项目统一） | `audio_capture.cc` |

## 3. 格式

- 缩进 **2 空格**，不用 Tab。
- 行宽 **100 列**（PRD §2.2：基准 80，团队统一放宽到 100，须一致）。
- 头文件用 `#pragma once`（全项目统一一种 guard）。
- 头文件内 `#include` 排序由 clang-format（Google 分组）处理。

## 4. 现代 C++ 用法

- 用 `nullptr`、`enum class`、`constexpr`、`override`。
- 资源用 `std::unique_ptr` / `std::shared_ptr`；避免裸 `new`/`delete`。
- `auto` 适度使用（不损害可读性时）。
- 不可拷贝类型显式 `= delete` 拷贝构造/赋值（如各持有设备/线程的模块）。
- 实现细节用 PIMPL（`struct Impl;`）隔离第三方头，缩短编译依赖。

## 5. 注释与文档

- 接口注释写在**头文件**；说明职责、线程约束、单位与帧长。
- 遵循 Google 注释规范；待办用 `TODO(name): ...` 或 `TODO(Mx): ...`（关联里程碑）。
- 魔数集中到 `audio_constants.h`，不散落。

## 6. 错误处理

- 音频实时路径**避免抛异常**；错误用返回值/状态码传递（PRD §2.2）。
- 公有接口用 `bool` / 状态枚举表达失败，调用方负责记录日志。

## 7. 实时/线程约束（PRD §10、§15，硬性）

1. **回调禁阻塞**：采集/播放回调（miniaudio 实时线程）只搬数据 + 算电平，
   **不分配大内存、不加锁等待、不打日志**。重活全在处理线程。
2. **跨线程通信**：
   - 采集/播放 ↔ 处理线程：用 SPSC 无锁环形缓冲（`ring_buffer.h`）。
   - UI ↔ 处理线程：状态用 `std::atomic`；日志用加锁小队列（`message_log.h`）。
3. **帧长对齐**：采集(任意) → APM(10ms/160) → VAD(512/32ms) → Opus(20ms/320)
   帧长各不相同，模块间必须靠累积缓冲**重新切帧**，禁止假设直通（§15.1）。
4. **AEC 参考信号**：播放每帧 10ms 也要喂给 `IAudioProcessor::ProcessRenderRef`，
   且与采集帧时间对齐，否则 AEC 无效（§15.2）。
5. **Barge-in 要快且彻底**：VAD `kSpeechStart` 时若在播放 → 立即
   `playback.Flush()` + `transport.CancelPendingAudio()` + 复位 VAD（§15.4）。
6. **Silero VAD 输入尺寸**：严格按模型版本喂样本数（v4/v5 = 512@16k），
   尺寸不符会报错或输出无意义（§15.6）。

## 8. 第三方库边界

- Google 规范只管 `src/`。`third_party/` 开源库**保持原样**，配 clang-format
  时务必排除，避免大规模无意义 diff（§15.7）。
- 对开源库的封装层（如 `audio_capture.cc`）属于自有代码，遵循本规范。
