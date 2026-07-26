#include "app_state.h"
#include <cmath>
#include <cstring>

std::atomic<bool> g_run{false};
std::atomic<bool> g_busy{false};
std::atomic<int> g_activeStreams{0};
std::atomic<bool> g_mediaFetch{false};
HWND g_wnd = nullptr;
std::wstring g_status = L"Add an Input, pick devices, connect, then Start.";
std::mutex g_statusMu;

float g_bars[kBars]{};
std::mutex g_barsMu;
std::atomic<float> g_meterIn{0.f};
std::atomic<float> g_meterOut{0.f};

std::vector<AudioApp> g_apps;
std::vector<OutDevice> g_outs;
std::vector<std::thread> g_threads;

std::wstring g_mediaTitle;
std::wstring g_mediaArtist;
std::wstring g_mediaHint;
HBITMAP g_thumbBmp = nullptr;
std::mutex g_mediaMu;

std::wstring g_cableId;
std::wstring g_cableName;

std::mutex g_liveFxMu;
std::vector<LiveFxBinding> g_liveFx;

void setStatus(const std::wstring& s) {
    { std::lock_guard lock(g_statusMu); g_status = s; }
    if (g_wnd) PostMessageW(g_wnd, WM_APP + 1, 0, 0);
}

void clearLiveFx() {
    std::lock_guard lock(g_liveFxMu);
    g_liveFx.clear();
}

void clearMedia() {
    std::lock_guard lock(g_mediaMu);
    g_mediaTitle.clear();
    g_mediaArtist.clear();
    if (g_thumbBmp) { DeleteObject(g_thumbBmp); g_thumbBmp = nullptr; }
}

void pushMeter(std::atomic<float>& meter, const BYTE* data, UINT32 frames, WORD ch) {
    if (!data || !frames || !ch) return;
    const auto* s = reinterpret_cast<const int16_t*>(data);
    float peak = 0.f;
    for (UINT32 f = 0; f < frames; ++f) {
        const float a = fabsf(s[f * ch] / 32768.f);
        if (a > peak) peak = a;
        if (ch > 1) {
            const float b = fabsf(s[f * ch + 1] / 32768.f);
            if (b > peak) peak = b;
        }
    }
    float cur = meter.load(std::memory_order_relaxed);
    if (peak > cur) cur = peak;
    else cur = cur * 0.88f + peak * 0.12f;
    meter.store(cur, std::memory_order_relaxed);
}

void pushViz(const BYTE* data, UINT32 frames, WORD ch) {
    if (!data || !frames) return;
    const auto* s = reinterpret_cast<const int16_t*>(data);
    {
        std::lock_guard lock(g_barsMu);
        static UINT32 w = 0;
        for (UINT32 f = 0; f < frames; ++f) {
            const float a = fabsf(s[f * ch] / 32768.f);
            g_bars[w % kBars] = (std::max)(g_bars[w % kBars] * 0.9f, a);
            if ((f & 7) == 0) { ++w; g_bars[w % kBars] *= 0.85f; }
        }
    }
    pushMeter(g_meterOut, data, frames, ch);
}

void joinStreams() {
    for (auto& t : g_threads) if (t.joinable()) t.join();
    g_threads.clear();
}
