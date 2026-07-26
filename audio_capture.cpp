#include "audio_capture.h"
#include "audio_render.h"
#include "app_state.h"
#include "common.h"

#ifndef AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
enum AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
};
enum PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
};
struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
};
struct AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union { AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams; };
};
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif

class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler {
public:
    ActivateHandler() : refs_(1), hr_(E_FAIL), client_(nullptr), marshaler_(nullptr) {
        ev_.h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        CoCreateFreeThreadedMarshaler((IUnknown*)(IActivateAudioInterfaceCompletionHandler*)this, &marshaler_);
    }
    ~ActivateHandler() {
        if (client_) client_->Release();
        if (marshaler_) marshaler_->Release();
    }
    IAudioClient* wait(HRESULT* out) {
        WaitForSingleObject(ev_.h, INFINITE);
        if (out) *out = hr_;
        IAudioClient* c = client_; client_ = nullptr; return c;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = (IActivateAudioInterfaceCompletionHandler*)this; AddRef(); return S_OK;
        }
        return marshaler_ ? marshaler_->QueryInterface(riid, ppv) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&refs_);
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT ah = E_FAIL; IUnknown* u = nullptr;
        HRESULT hr = op->GetActivateResult(&ah, &u);
        hr_ = FAILED(hr) ? hr : ah;
        if (SUCCEEDED(hr_) && u) {
            if (FAILED(u->QueryInterface(__uuidof(IAudioClient), (void**)&client_))) client_ = nullptr;
            u->Release();
        }
        SetEvent(ev_.h);
        return S_OK;
    }
private:
    LONG refs_; HRESULT hr_; IAudioClient* client_; IUnknown* marshaler_; Handle ev_;
};

static bool streamWithClient(IAudioClient* client, DWORD streamFlags, const std::wstring& outId, FxChain* fx) {
    WAVEFORMATEX fmt = pcm48();
    Handle ev; ev.h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            streamFlags | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
            10000000, 0, &fmt, nullptr))) {
        setStatus(L"Capture init failed."); return false;
    }
    client->SetEventHandle(ev.h);
    ComPtr<IAudioCaptureClient> cap;
    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap)) || !cap) return false;
    return pumpAudio(client, cap.get(), ev.h, fmt, outId, fx);
}

bool streamLoopback(DWORD pid, const std::wstring& outId, FxChain* fx) {
    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv{}; pv.vt = VT_BLOB; pv.blob.cbSize = sizeof(ap); pv.blob.pBlobData = (BYTE*)&ap;

    auto* handler = new ActivateHandler();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    if (FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
            __uuidof(IAudioClient), &pv, handler, &op)) || !op) {
        setStatus(L"Loopback activate failed."); handler->Release(); return false;
    }
    HRESULT ah = E_FAIL;
    ComPtr<IAudioClient> client(handler->wait(&ah));
    handler->Release();
    if (FAILED(ah) || !client) { setStatus(L"Loopback activate failed."); return false; }
    return streamWithClient(client.get(), AUDCLNT_STREAMFLAGS_LOOPBACK, outId, fx);
}

bool streamCaptureDevice(const std::wstring& capId, const std::wstring& outId, FxChain* fx) {
    ComPtr<IMMDeviceEnumerator> en(makeEnumerator());
    if (!en) return false;
    ComPtr<IMMDevice> d;
    if (FAILED(en->GetDevice(capId.c_str(), &d)) || !d) {
        setStatus(L"Failed to open capture device."); return false;
    }
    ComPtr<IAudioClient> client;
    if (FAILED(d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) || !client)
        return false;
    return streamWithClient(client.get(), 0, outId, fx);
}
