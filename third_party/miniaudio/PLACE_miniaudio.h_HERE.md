# miniaudio

把单头文件 `miniaudio.h` 放到本目录:

- 下载: https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
- 许可证: Public Domain / MIT-0（保持原样，勿改格式 / 勿套 Google 规范）。

放好后，`src/audio_capture.cc`、`src/audio_playback.cc` 会通过
`__has_include("miniaudio.h")` 自动启用真实采集/播放（否则编译为可运行的
stub，Start() 返回 false）。`MINIAUDIO_IMPLEMENTATION` 仅在 `audio_capture.cc`
定义一次。
