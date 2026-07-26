#include "stream_job.h"
#include "audio_capture.h"
#include "audio_devices.h"
#include "audio_router.h"
#include "process_util.h"
#include <roapi.h>

void runJob(Job job) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    RoInitialize(RO_INIT_MULTITHREADED);

    std::wstring originalId = job.app.deviceId;
    if (originalId.empty())
        for (auto& o : g_outs) if (o.isDefault) { originalId = o.id; break; }
    const std::wstring originalName = job.app.deviceName;

    // park the app on a virtual cable so we can grab it cleanly
    OutDevice parking{};
    bool haveParking = false;
    if (!job.forceLoopback) {
        if (!job.cableId.empty() && job.cableId != job.out.id) {
            parking.id = job.cableId;
            parking.name = job.cableName.empty() ? L"Virtual Cable" : job.cableName;
            haveParking = true;
        }
        if (!haveParking) {
            for (auto& o : g_outs) {
                if (isCable(o.name) && o.id != job.out.id) { parking = o; haveParking = true; break; }
            }
        }
        if (!haveParking)
            for (auto& o : g_outs) if (o.id != job.out.id) { parking = o; haveParking = true; break; }
    }

    EndpointRouter router;
    bool routed = false;
    const std::wstring cableCap = findCaptureId(L"CABLE");

    if (!job.forceLoopback && haveParking && router.init()) {
        setAppDevice(router, job.app.name, parking.id);
        routed = true;
        setStatus(L"Routed " + job.app.name + L" \u2192 " + parking.name);
        Sleep(700); // give Windows a sec to actually move the app
    }

    int muted = 0;
    // don't let the same app also blast the real output while we're parking it
    if (!haveParking || job.out.name != parking.name)
        muted = muteAppOnDevice(job.app.name, job.out.name, true);

    bool ok = false;
    if (!job.forceLoopback && routed && haveParking && isCable(parking.name) && !cableCap.empty()) {
        setStatus(L"Streaming via VB-Cable \u2014 " + job.app.name);
        ok = streamCaptureDevice(cableCap, job.out.id, job.fx.get());
    } else {
        setStatus(L"Streaming \u2014 " + job.app.name + L" \u2192 " + job.out.name);
        ok = streamLoopback(treeRoot(job.app.pid), job.out.id, job.fx.get());
    }

    if (muted) muteAppOnDevice(job.app.name, job.out.name, false);

    if (routed) {
        if (!originalId.empty()) {
            setAppDevice(router, job.app.name, originalId);
            Sleep(300);
            setAppDevice(router, job.app.name, originalId);
            Sleep(300);
        }
    }

    const int left = --g_activeStreams;
    if (left <= 0) {
        g_activeStreams = 0;
        g_busy = false;
        setStatus(ok || !g_run ? L"Stopped." : L"Stream failed.");
        if (g_wnd) PostMessageW(g_wnd, WM_APP + 2, 0, 0);
    }

    RoUninitialize();
    CoUninitialize();
}
