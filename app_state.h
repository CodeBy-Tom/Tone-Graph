#pragma once

#include "common.h"
#include "audio_fx.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AudioApp {
    DWORD pid = 0;
    std::wstring name, title, deviceName, deviceId;
    bool playing = false;
};

struct OutDevice {
    std::wstring id, name;
    bool isDefault = false;
};

struct Job {
    AudioApp app;
    OutDevice out;
    std::wstring cableId;
    std::wstring cableName;
    bool forceLoopback = false;
    std::shared_ptr<FxChain> fx;
};

struct LiveFxBinding {
    std::shared_ptr<FxChain> fx;
    std::vector<int> effectNodes;
};

constexpr int kBars = 64;

extern std::atomic<bool> g_run;
extern std::atomic<bool> g_busy;
extern std::atomic<int> g_activeStreams;
extern std::atomic<bool> g_mediaFetch;
extern HWND g_wnd;
extern std::wstring g_status;
extern std::mutex g_statusMu;

extern float g_bars[kBars];
extern std::mutex g_barsMu;
extern std::atomic<float> g_meterIn;  // before FX
extern std::atomic<float> g_meterOut; // after FX

extern std::vector<AudioApp> g_apps;
extern std::vector<OutDevice> g_outs;
extern std::vector<std::thread> g_threads;

extern std::mutex g_liveFxMu;
extern std::vector<LiveFxBinding> g_liveFx;

extern std::wstring g_mediaTitle;
extern std::wstring g_mediaArtist;
extern std::wstring g_mediaHint;
extern HBITMAP g_thumbBmp;
extern std::mutex g_mediaMu;

extern std::wstring g_cableId;
extern std::wstring g_cableName;

void setStatus(const std::wstring& s);
void clearMedia();
void pushViz(const BYTE* data, UINT32 frames, WORD ch); // bars + out meter
void pushMeter(std::atomic<float>& meter, const BYTE* data, UINT32 frames, WORD ch);
void joinStreams();
void clearLiveFx();
