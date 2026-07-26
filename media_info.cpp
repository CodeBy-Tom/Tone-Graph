#include "media_info.h"
#include "app_state.h"
#include "common.h"

#include <wincodec.h>
#include <thread>
#include <vector>

#undef GetCurrentTime
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

static HBITMAP hbitmapFromEncodedBytes(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return nullptr;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))) || !factory) return nullptr;

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || !stream) return nullptr;
    if (FAILED(stream->InitializeFromMemory((BYTE*)bytes.data(), (DWORD)bytes.size()))) return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) || !decoder)
        return nullptr;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return nullptr;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv)) || !conv) return nullptr;
    if (FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return nullptr;

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (!w || !h) return nullptr;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP result = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (result && bits) {
        const UINT stride = w * 4;
        conv->CopyPixels(nullptr, stride, stride * h, (BYTE*)bits);
    }
    return result;
}

static bool wideContainsI(std::wstring hay, std::wstring needle) {
    if (needle.empty()) return true;
    for (auto& c : hay) c = (wchar_t)towlower(c);
    for (auto& c : needle) c = (wchar_t)towlower(c);
    if (needle.size() > 4 && needle.substr(needle.size() - 4) == L".exe") needle.resize(needle.size() - 4);
    return hay.find(needle) != std::wstring::npos;
}

void fetchNowPlaying(std::wstring appHint) {
    if (g_mediaFetch.exchange(true)) return;
    std::thread([appHint]() {
        std::wstring title, artist;
        HBITMAP bmp = nullptr;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            using namespace winrt::Windows::Media::Control;
            using namespace winrt::Windows::Storage::Streams;

            auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            GlobalSystemMediaTransportControlsSession session{ nullptr };

            auto sessions = manager.GetSessions();
            for (uint32_t i = 0; i < sessions.Size(); ++i) {
                auto s = sessions.GetAt(i);
                if (wideContainsI(s.SourceAppUserModelId().c_str(), appHint)) { session = s; break; }
            }
            if (!session) session = manager.GetCurrentSession();

            if (session) {
                auto props = session.TryGetMediaPropertiesAsync().get();
                title = props.Title().c_str();
                artist = props.Artist().c_str();
                if (auto thumb = props.Thumbnail()) {
                    auto ras = thumb.OpenReadAsync().get();
                    uint32_t size = (uint32_t)ras.Size();
                    if (size > 0 && size < 8 * 1024 * 1024) {
                        Buffer buffer(size);
                        ras.ReadAsync(buffer, size, InputStreamOptions::None).get();
                        auto reader = DataReader::FromBuffer(buffer);
                        std::vector<BYTE> bytes(size);
                        reader.ReadBytes(bytes);
                        bmp = hbitmapFromEncodedBytes(bytes);
                    }
                }
            }
        } catch (...) {}

        {
            std::lock_guard lock(g_mediaMu);
            g_mediaTitle = std::move(title);
            g_mediaArtist = std::move(artist);
            if (g_thumbBmp) DeleteObject(g_thumbBmp);
            g_thumbBmp = bmp;
        }
        g_mediaFetch = false;
        if (g_wnd) PostMessageW(g_wnd, WM_APP + 3, 0, 0);
    }).detach();
}
