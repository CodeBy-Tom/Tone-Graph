#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

template<typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    explicit ComPtr(T* q) : p(q) {}
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { reset(); }
    void reset(T* q = nullptr) { if (p) p->Release(); p = q; }
    T* get() const { return p; }
    T** operator&() { reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T* detach() { T* q = p; p = nullptr; return q; }
};

struct Handle {
    HANDLE h = nullptr;
    ~Handle() { if (h) CloseHandle(h); }
    explicit operator bool() const { return h != nullptr; }
};

inline IMMDeviceEnumerator* makeEnumerator() {
    IMMDeviceEnumerator* en = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&en);
    return en;
}

inline std::wstring deviceIdOf(IMMDevice* d) {
    LPWSTR id = nullptr;
    std::wstring out;
    if (SUCCEEDED(d->GetId(&id)) && id) { out = id; CoTaskMemFree(id); }
    return out;
}

inline std::wstring friendlyName(IMMDevice* d) {
    ComPtr<IPropertyStore> ps;
    if (FAILED(d->OpenPropertyStore(STGM_READ, &ps)) || !ps) return L"(unknown)";
    PROPVARIANT v; PropVariantInit(&v);
    std::wstring name = L"(unknown)";
    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = v.pwszVal;
    PropVariantClear(&v);
    return name;
}

template<typename Fn>
inline void forEachEndpoint(EDataFlow flow, Fn&& fn) {
    ComPtr<IMMDeviceEnumerator> en(makeEnumerator());
    if (!en) return;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)) || !col) return;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        ComPtr<IMMDevice> d;
        if (SUCCEEDED(col->Item(i, &d)) && d) fn(d.get());
    }
}

template<typename Fn>
inline void forEachSession(IMMDevice* d, Fn&& fn) {
    ComPtr<IAudioSessionManager2> sm;
    if (FAILED(d->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&sm)) || !sm) return;
    ComPtr<IAudioSessionEnumerator> se;
    if (FAILED(sm->GetSessionEnumerator(&se)) || !se) return;
    int n = 0; se->GetCount(&n);
    for (int i = 0; i < n; ++i) {
        ComPtr<IAudioSessionControl> c;
        if (FAILED(se->GetSession(i, &c)) || !c) continue;
        ComPtr<IAudioSessionControl2> c2;
        if (FAILED(c->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2)) || !c2) continue;
        fn(c.get(), c2.get());
    }
}
