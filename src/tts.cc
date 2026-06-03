#include "tts.h"

namespace voice {

#if defined(VOICE_ENABLE_TTS) && defined(_WIN32)

}  // namespace voice

// SAPI5 头依赖 Windows 头, 放在 namespace 外避免污染。
#include <windows.h>

#include <objbase.h>
#include <sapi.h>

namespace voice {

namespace {

// UTF-8 -> UTF-16 (SAPI Speak 需 WCHAR)。
std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      &w[0], n);
  return w;
}

// 基于 SAPI5 的合成: 让 SpVoice 输出到 16k/mono/16bit 内存流, 读出裸 PCM。
class Sapi5Tts : public ITextToSpeech {
 public:
  ~Sapi5Tts() override {
    if (voice_) voice_->Release();
    if (com_owned_) CoUninitialize();
  }

  bool Init() override {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_OK/S_FALSE: 本对象负责 CoUninitialize; RPC_E_CHANGED_MODE: 已被他处
    // 初始化为别的套间模型, 仍可用但不由我们反初始化。
    com_owned_ = (hr == S_OK || hr == S_FALSE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    return SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                      IID_ISpVoice,
                                      reinterpret_cast<void**>(&voice_)));
  }

  Pcm16 Synthesize(const std::string& utf8_text) override {
    Pcm16 out;
    if (!voice_) return out;
    const std::wstring wtext = Utf8ToWide(utf8_text);
    if (wtext.empty()) return out;

    IStream* base = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &base))) return out;

    ISpStream* spstream = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL,
                                IID_ISpStream,
                                reinterpret_cast<void**>(&spstream)))) {
      base->Release();
      return out;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;                  // 单声道。
    wfx.nSamplesPerSec = 16000;         // 16 kHz。
    wfx.wBitsPerSample = 16;            // int16。
    wfx.nBlockAlign =
        static_cast<WORD>(wfx.nChannels * wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    bool ok = SUCCEEDED(spstream->SetBaseStream(base, SPDFID_WaveFormatEx,
                                                &wfx));
    if (ok) ok = SUCCEEDED(voice_->SetOutput(spstream, TRUE));
    if (ok) {
      // 同步合成 (本调用在控制线程, 非实时回调, 短暂阻塞可接受)。
      ok = SUCCEEDED(voice_->Speak(wtext.c_str(), SPF_DEFAULT, nullptr));
      // 确保合成真正写完流再读取 (防御异步/早返回)。
      voice_->WaitUntilDone(INFINITE);
    }

    if (ok) {
      // base 为 CreateStreamOnHGlobal 创建, 取其 HGLOBAL 直接拷出裸 PCM。
      HGLOBAL hg = nullptr;
      if (SUCCEEDED(GetHGlobalFromStream(base, &hg)) && hg) {
        const SIZE_T bytes = GlobalSize(hg);
        const size_t samples = static_cast<size_t>(bytes) / sizeof(int16_t);
        if (samples > 0) {
          void* p = GlobalLock(hg);
          if (p) {
            out.resize(samples);
            memcpy(out.data(), p, samples * sizeof(int16_t));
            GlobalUnlock(hg);
          }
        }
      }
    }

    spstream->Release();
    base->Release();
    return out;
  }

 private:
  ISpVoice* voice_ = nullptr;
  bool com_owned_ = false;
};

}  // namespace

std::unique_ptr<ITextToSpeech> CreateTts() {
  return std::make_unique<Sapi5Tts>();
}

#else  // !VOICE_ENABLE_TTS || !_WIN32 —— stub。

namespace {

// 未启用 TTS 或非 Windows: 合成永远返回空 (由调用方记日志)。
class NullTts : public ITextToSpeech {
 public:
  bool Init() override { return true; }
  Pcm16 Synthesize(const std::string&) override { return Pcm16(); }
};

}  // namespace

std::unique_ptr<ITextToSpeech> CreateTts() {
  return std::make_unique<NullTts>();
}

#endif

}  // namespace voice
