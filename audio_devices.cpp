#include "audio_devices.h"
#include "process_util.h"
#include <set>

bool isCable(const std::wstring& n) {
    return n.find(L"CABLE") != std::wstring::npos
        || n.find(L"VB-Audio") != std::wstring::npos
        || n.find(L"Voicemeeter") != std::wstring::npos;
}

std::vector<AudioApp> listApps() {
    std::vector<AudioApp> apps;
    std::set<std::wstring> seen;
    forEachEndpoint(eRender, [&](IMMDevice* d) {
        const std::wstring dname = friendlyName(d), did = deviceIdOf(d);
        int idx = 0;
        forEachSession(d, [&](IAudioSessionControl* c, IAudioSessionControl2* c2) {
            const int s = idx++;
            if (c2->IsSystemSoundsSession() == S_OK) return;
            AudioSessionState st = AudioSessionStateInactive;
            c->GetState(&st);
            if (st == AudioSessionStateExpired) return;

            LPWSTR sid = nullptr;
            std::wstring key;
            if (SUCCEEDED(c2->GetSessionInstanceIdentifier(&sid)) && sid) { key = sid; CoTaskMemFree(sid); }
            else key = dname + L"#" + std::to_wstring(s);
            if (!seen.insert(key).second) return;

            AudioApp a;
            a.deviceName = dname;
            a.deviceId = did;
            a.playing = (st == AudioSessionStateActive);
            DWORD pid = 0;
            if (SUCCEEDED(c2->GetProcessId(&pid)) && pid) {
                a.pid = pid;
                a.name = nameFromPid(pid);
            } else {
                a.name = L"(multi-process app)";
            }
            LPWSTR title = nullptr;
            if (SUCCEEDED(c->GetDisplayName(&title)) && title) {
                if (title[0] && title[0] != L'@') a.title = title;
                CoTaskMemFree(title);
            }
            apps.push_back(std::move(a));
        });
    });
    return apps;
}

std::vector<OutDevice> listOutputs() {
    std::vector<OutDevice> outs;
    std::wstring defId;
    {
        ComPtr<IMMDeviceEnumerator> en(makeEnumerator());
        ComPtr<IMMDevice> def;
        if (en && SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && def)
            defId = deviceIdOf(def.get());
    }
    forEachEndpoint(eRender, [&](IMMDevice* d) {
        OutDevice o;
        o.id = deviceIdOf(d);
        o.isDefault = (o.id == defId);
        o.name = friendlyName(d);
        if (!o.id.empty()) outs.push_back(std::move(o));
    });
    return outs;
}

std::wstring findCaptureId(const wchar_t* needle) {
    std::wstring found;
    forEachEndpoint(eCapture, [&](IMMDevice* d) {
        if (!found.empty()) return;
        if (friendlyName(d).find(needle) != std::wstring::npos) found = deviceIdOf(d);
    });
    return found;
}

int muteAppOnDevice(const std::wstring& exe, const std::wstring& devName, bool mute) {
    int n = 0;
    forEachEndpoint(eRender, [&](IMMDevice* d) {
        if (friendlyName(d) != devName) return;
        forEachSession(d, [&](IAudioSessionControl* c, IAudioSessionControl2* c2) {
            DWORD pid = 0;
            if (FAILED(c2->GetProcessId(&pid)) || !pid || nameFromPid(pid) != exe) return;
            ComPtr<ISimpleAudioVolume> vol;
            if (SUCCEEDED(c->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&vol)) && vol)
                if (SUCCEEDED(vol->SetMute(mute ? TRUE : FALSE, nullptr))) ++n;
        });
    });
    return n;
}
