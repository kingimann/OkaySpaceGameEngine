#pragma once
// WASAPI shared-mode render output for the UWP app: the Windows side of XboxAudio.hpp's AudioHost. UNCOMPILED on Windows (syntax-checked
// with MinGW headers only) and NEVER RUN. Written from Microsoft's documentation: "Rendering a stream" (IAudioClient::Initialize with
// AUDCLNT_STREAMFLAGS_EVENTCALLBACK, SetEventHandle, GetBufferSize / GetCurrentPadding, IAudioRenderClient::GetBuffer / ReleaseBuffer)
// and "Activating a WASAPI interface in a UWP app" (ActivateAudioInterfaceAsync with an IActivateAudioInterfaceCompletionHandler that is
// also an IAgileObject; UWP has no IMMDeviceEnumerator). The device id comes from the app shell: Windows.Media.Devices.MediaDevice::
// GetDefaultAudioRenderId(AudioDeviceRole::Default). The render thread is a native CreateThread (no <thread>), the stop flag an atomic.
// Only a 32-bit float mix format is accepted (what shared mode normally reports); anything else reports "no audio" instead of guessing.
#if defined(_WIN32) && defined(OKAY_XBOX_UWP)
#include "XboxAudio.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <atomic>
#include <string>

#if defined(__MINGW32__)   // only for the Linux syntax check: MinGW's header lacks the declaration, the Windows SDK's mmdeviceapi.h has it
extern "C" HRESULT WINAPI ActivateAudioInterfaceAsync(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);
#endif

namespace okay::xbox {

namespace detail {
// Completion handler: signals an event; must be agile (callable from any thread).
class ActivateHandler : public IActivateAudioInterfaceCompletionHandler {
public:
    explicit ActivateHandler(HANDLE ev) : ev_(ev) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++ref_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = (ULONG)--ref_; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) { *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this); AddRef(); return S_OK; }
        *out = nullptr; return E_NOINTERFACE;   // (IAgileObject: not answered here; see docs/xbox_uwp.md, "Audio", UNVERIFIED)
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override { SetEvent(ev_); return S_OK; }
private:
    std::atomic<long> ref_{1}; HANDLE ev_;
};
}

class WasapiHost : public AudioHost {
public:
    /// `renderDeviceId`: the default render device's id string from MediaDevice::GetDefaultAudioRenderId.
    explicit WasapiHost(std::wstring renderDeviceId) : id_(std::move(renderDeviceId)) {}
    ~WasapiHost() override { Stop(nullptr); }

    bool Format(AudioOutputFormat& out) override {
        if (!Open()) return false;
        out.rate = (int)fmt_->nSamplesPerSec; out.channels = (int)fmt_->nChannels; return true;
    }
    bool Start(UwpAudioDevice* dev, std::string& err) override {
        if (!Open()) { err = err_; return false; }
        if (thread_) { err = "audio output is already running"; return false; }
        dev_ = dev; stop_.store(false);
        if (FAILED(client_->Start())) { err = "IAudioClient::Start failed"; return false; }
        thread_ = CreateThread(nullptr, 0, &WasapiHost::Entry, this, 0, nullptr);
        if (!thread_) { client_->Stop(); err = "could not start the audio thread"; return false; }
        return true;
    }
    void Stop(UwpAudioDevice*) override {
        if (thread_) {
            stop_.store(true); if (event_) SetEvent(event_);
            WaitForSingleObject(thread_, 3000);   // the thread must be out of Render before the device is destroyed
            CloseHandle(thread_); thread_ = nullptr;
            if (client_) client_->Stop();
        }
        dev_ = nullptr;
    }
    const std::string& Error() const { return err_; }

private:
    bool Open() {
        if (client_) return true;
        if (id_.empty()) { err_ = "no audio device id"; return false; }
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        detail::ActivateHandler* h = new detail::ActivateHandler(done);
        IActivateAudioInterfaceAsyncOperation* op = nullptr;
        HRESULT hr = ActivateAudioInterfaceAsync(id_.c_str(), __uuidof(IAudioClient), nullptr, h, &op);
        if (SUCCEEDED(hr)) {
            WaitForSingleObject(done, 5000);
            HRESULT act = E_FAIL; IUnknown* unk = nullptr;
            hr = op->GetActivateResult(&act, &unk);
            if (SUCCEEDED(hr) && SUCCEEDED(act) && unk) { unk->QueryInterface(__uuidof(IAudioClient), (void**)&client_); unk->Release(); }
            op->Release();
        }
        h->Release(); CloseHandle(done);
        if (!client_) { err_ = "could not activate the audio device"; return false; }
        if (FAILED(client_->GetMixFormat(&fmt_)) || !fmt_) { err_ = "no mix format"; Reset(); return false; }
        const bool isFloat = fmt_->wBitsPerSample == 32 && (fmt_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || fmt_->wFormatTag == WAVE_FORMAT_EXTENSIBLE);
        if (!isFloat) { err_ = "the audio mix format is not 32-bit float"; Reset(); return false; }
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, fmt_, nullptr)) ||
            FAILED(client_->SetEventHandle(event_)) || FAILED(client_->GetBufferSize(&bufFrames_)) ||
            FAILED(client_->GetService(__uuidof(IAudioRenderClient), (void**)&render_))) { err_ = "could not set up the audio stream"; Reset(); return false; }
        return true;
    }
    void Reset() {
        if (render_) { render_->Release(); render_ = nullptr; }
        if (client_) { client_->Release(); client_ = nullptr; }
        if (fmt_) { CoTaskMemFree(fmt_); fmt_ = nullptr; }
        if (event_) { CloseHandle(event_); event_ = nullptr; }
    }
    static DWORD WINAPI Entry(LPVOID p) { static_cast<WasapiHost*>(p)->Loop(); return 0; }
    void Loop() {
        while (!stop_.load()) {
            if (WaitForSingleObject(event_, 200) == WAIT_FAILED) break;
            if (stop_.load()) break;
            UINT32 pad = 0; if (FAILED(client_->GetCurrentPadding(&pad))) continue;
            const UINT32 frames = bufFrames_ > pad ? bufFrames_ - pad : 0;
            if (!frames) continue;
            BYTE* data = nullptr;
            if (FAILED(render_->GetBuffer(frames, &data)) || !data) continue;
            if (dev_) dev_->Render(reinterpret_cast<float*>(data), (int)frames); else std::memset(data, 0, (std::size_t)frames * fmt_->nBlockAlign);
            render_->ReleaseBuffer(frames, 0);
        }
    }
    std::wstring id_; std::string err_;
    IAudioClient* client_ = nullptr; IAudioRenderClient* render_ = nullptr; WAVEFORMATEX* fmt_ = nullptr;
    HANDLE event_ = nullptr; HANDLE thread_ = nullptr; UINT32 bufFrames_ = 0;
    std::atomic<bool> stop_{false}; UwpAudioDevice* dev_ = nullptr;
};

} // namespace okay::xbox
#endif
