# 获取 WebRTC APM 预编译产物 (M6 / AC-8)。
#
# 用 get-wrecked/webrtc-audioprocessing 的 M124 win-x64 预编译包: 单个
# audio_processing.lib 已静态包含 abseil 等依赖，可直接链接进 MSVC 工程，
# 避开 webrtc-audio-processing 的 Meson/clang 构建难题 (PRD §3)。
#
# 用法 (在本目录运行):
#   powershell -ExecutionPolicy Bypass -File .\fetch_webrtc_apm.ps1
# 完成后 third_party/webrtc-apm/extracted/{lib,src} 就位，CMake 配置时
# -DVOICE_ENABLE_WEBRTC=ON 会自动检测并链接。
#
# 其他平台: 同一 release 提供 mac-x64 / mac-arm64 / win-arm64 包，按需替换。
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$tag = "M124"
$asset = "webrtc-audioprocessing-$tag-win-x64.zip"
$url = "https://github.com/get-wrecked/webrtc-audioprocessing/releases/download/$tag/$asset"
$zip = Join-Path $here $asset
$dst = Join-Path $here "extracted"

Write-Host "Downloading $url ..."
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
Expand-Archive -Path $zip -DestinationPath $dst -Force

if (Test-Path (Join-Path $dst "lib\audio_processing.lib")) {
    Write-Host "OK: WebRTC APM ready at $dst"
} else {
    throw "Extraction failed: audio_processing.lib not found"
}
