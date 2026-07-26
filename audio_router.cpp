#include "audio_router.h"
#include "common.h"
#include "process_util.h"

#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <activation.h>

#define STUB virtual HRESULT STDMETHODCALLTYPE

// undocumented WinRT audio-policy COM — used to move an app to another device
MIDL_INTERFACE("ab3d4648-e242-459f-b02f-541c70306324")
IAudioPolicyConfig21H2 : public IInspectable {
    STUB s01() = 0; STUB s02() = 0; STUB s03() = 0; STUB s04() = 0;
    STUB s05() = 0; STUB s06() = 0; STUB s07() = 0; STUB s08() = 0;
    STUB s09() = 0; STUB s10() = 0; STUB s11() = 0; STUB s12() = 0;
    STUB s13() = 0; STUB s14() = 0; STUB s15() = 0; STUB s16() = 0;
    STUB s17() = 0; STUB s18() = 0; STUB s19() = 0;
    STUB SetPersistedDefaultAudioEndpoint(UINT32, EDataFlow, ERole, HSTRING) = 0;
    STUB GetPersistedDefaultAudioEndpoint(UINT32, EDataFlow, ERole, HSTRING*) = 0;
    STUB ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

MIDL_INTERFACE("2a59116d-6c4f-45e0-a74f-707e3fef9258")
IAudioPolicyConfigDownlevel : public IInspectable {
    STUB SetPersistedDefaultAudioEndpoint(UINT32, EDataFlow, ERole, HSTRING) = 0;
    STUB GetPersistedDefaultAudioEndpoint(UINT32, EDataFlow, ERole, HSTRING*) = 0;
    STUB ClearAllPersistedApplicationDefaultEndpoints() = 0;
};
#undef STUB

struct EndpointRouter::Impl {
    IAudioPolicyConfig21H2* f21 = nullptr;
    IAudioPolicyConfigDownlevel* fDown = nullptr;
    ~Impl() {
        if (f21) f21->Release();
        if (fDown) fDown->Release();
    }
};

static std::wstring packRenderId(std::wstring id) {
    if (id.find(L"SWD#MMDEVAPI#") != std::wstring::npos) return id;
    return L"\\\\?\\SWD#MMDEVAPI#" + id + L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
}

EndpointRouter::EndpointRouter() : impl_(new Impl) {}
EndpointRouter::~EndpointRouter() { delete impl_; }

bool EndpointRouter::init() {
    const wchar_t* cls = L"Windows.Media.Internal.AudioPolicyConfig";
    HSTRING hs = nullptr;
    if (FAILED(WindowsCreateString(cls, (UINT32)wcslen(cls), &hs))) return false;
    IInspectable* insp = nullptr;
    HRESULT hr = RoGetActivationFactory(hs, __uuidof(IInspectable), (void**)&insp);
    WindowsDeleteString(hs);
    if (FAILED(hr) || !insp) {
        HMODULE m = LoadLibraryW(L"AudioSes.dll");
        if (!m) return false;
        auto fn = (HRESULT(WINAPI*)(HSTRING, IActivationFactory**))GetProcAddress(m, "DllGetActivationFactory");
        if (!fn) return false;
        if (FAILED(WindowsCreateString(cls, (UINT32)wcslen(cls), &hs))) return false;
        IActivationFactory* f = nullptr;
        hr = fn(hs, &f);
        WindowsDeleteString(hs);
        if (FAILED(hr) || !f) return false;
        insp = f;
    }
    if (FAILED(insp->QueryInterface(__uuidof(IAudioPolicyConfig21H2), (void**)&impl_->f21))) impl_->f21 = nullptr;
    if (!impl_->f21 && FAILED(insp->QueryInterface(__uuidof(IAudioPolicyConfigDownlevel), (void**)&impl_->fDown)))
        impl_->fDown = nullptr;
    insp->Release();
    return impl_->f21 || impl_->fDown;
}

bool EndpointRouter::set(DWORD pid, const std::wstring& deviceId) {
    HSTRING hs = nullptr;
    if (!deviceId.empty()) {
        std::wstring id = packRenderId(deviceId);
        if (FAILED(WindowsCreateString(id.c_str(), (UINT32)id.size(), &hs))) return false;
    }
    bool ok = true;
    for (ERole role : { eConsole, eMultimedia, eCommunications }) {
        HRESULT hr = impl_->f21
            ? impl_->f21->SetPersistedDefaultAudioEndpoint(pid, eRender, role, hs)
            : impl_->fDown->SetPersistedDefaultAudioEndpoint(pid, eRender, role, hs);
        if (FAILED(hr) && hr != (HRESULT)0x88890001) ok = false;
    }
    if (hs) WindowsDeleteString(hs);
    return ok;
}

void setAppDevice(EndpointRouter& r, const std::wstring& exe, const std::wstring& deviceId) {
    for (DWORD pid : pidsNamed(exe)) r.set(pid, deviceId);
}
