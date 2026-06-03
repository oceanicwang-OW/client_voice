# models/

放置运行期模型文件。

- `silero_vad.onnx` —— Silero VAD 微型检测网络（约 1~2 MB，仅判断“当前是否
  有人说话”，无语言/生成能力）。
  - 来源: https://github.com/snakers4/silero-vad
  - 启用方式: CMake 打开 `-DVOICE_ENABLE_SILERO=ON`，并确保 onnxruntime 的
    DLL 与可执行文件同目录部署（PRD §12）。
- 未启用 Silero 时（默认），VAD 使用纯能量端点检测占位实现，无需模型文件
  （对应 PRD §11“零模型文件”降级路径之一）。

注意: `*.onnx` 已在 `.gitignore` 中忽略，按发布流程分发而非提交入库。
