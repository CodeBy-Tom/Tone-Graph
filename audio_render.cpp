#include "audio_render.h"
#include "app_state.h"
#include <cstring>
#include <mutex>
#include <vector>

WAVEFORMATEX pcm48() {
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = 48000;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = 192000;
    return f;
}

bool pumpAudio(IAudioClient* capClient, IAudioCaptureClient* cap, HANDLE ready,
               const WAVEFORMATEX& fmt, const std::wstring& outId, FxChain* fx) {
    ComPtr<IMMDeviceEnumerator> en(makeEnumerator());
    if (!en) return false;
    ComPtr<IMMDevice> outDev;
    if (FAILED(en->GetDevice(outId.c_str(), &outDev)) || !outDev) {
        setStatus(L"Failed to open output device."); return false;
    }

    ComPtr<IAudioClient> ren;
    if (FAILED(outDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ren)) || !ren)
        return false;

    WAVEFORMATEX f = fmt;
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
    if (FAILED(ren->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 10000000, 0, &f, nullptr))) {
        setStatus(L"Render init failed."); return false;
    }

    UINT32 bufFrames = 0; ren->GetBufferSize(&bufFrames);
    ComPtr<IAudioRenderClient> rc;
    if (FAILED(ren->GetService(__uuidof(IAudioRenderClient), (void**)&rc)) || !rc) return false;

    if (fx) fx->prepare((float)fmt.nSamplesPerSec);

    BYTE* silence = nullptr;
    UINT32 pre = bufFrames / 2;
    if (SUCCEEDED(rc->GetBuffer(pre, &silence)) && silence) {
        memset(silence, 0, (size_t)pre * fmt.nBlockAlign);
        rc->ReleaseBuffer(pre, 0);
    }
    if (FAILED(ren->Start()) || FAILED(capClient->Start())) return false;

    setStatus(L"Streaming\u2026");
    std::vector<BYTE> scratch;
    while (g_run) {
        WaitForSingleObject(ready, 50);
        UINT32 pkt = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt) {
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;

            const BYTE* src = data;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                pushMeter(g_meterIn, data, frames, fmt.nChannels); // input meter = raw capture
                if (fx) {
                    bool hasFx = false;
                    { std::lock_guard lock(fx->mu); hasFx = !fx->steps.empty(); }
                    if (hasFx) {
                        // copy so we don't mutate WASAPI's buffer
                        scratch.resize((size_t)frames * fmt.nBlockAlign);
                        memcpy(scratch.data(), data, scratch.size());
                        fx->process(reinterpret_cast<int16_t*>(scratch.data()), (int)frames, fmt.nChannels);
                        src = scratch.data();
                    }
                }
            }

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && src)
                pushViz(src, frames, fmt.nChannels); // out meter lives in here

            UINT32 pad = 0; ren->GetCurrentPadding(&pad);
            UINT32 n = (std::min)(frames, bufFrames - pad); // don't overrun the render buffer
            if (n) {
                BYTE* dst = nullptr;
                if (SUCCEEDED(rc->GetBuffer(n, &dst)) && dst) {
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !src) memset(dst, 0, (size_t)n * fmt.nBlockAlign);
                    else memcpy(dst, src, (size_t)n * fmt.nBlockAlign);
                    rc->ReleaseBuffer(n, 0);
                }
            }
            cap->ReleaseBuffer(frames);
        }
    }
    capClient->Stop(); ren->Stop();
    setStatus(L"Stopped.");
    return true;
}
