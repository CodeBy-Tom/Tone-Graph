#pragma once

#include <windows.h>
#include <string>

class EndpointRouter {
public:
    EndpointRouter();
    ~EndpointRouter();
    EndpointRouter(const EndpointRouter&) = delete;
    EndpointRouter& operator=(const EndpointRouter&) = delete;

    bool init();
    bool set(DWORD pid, const std::wstring& deviceId);

private:
    struct Impl;
    Impl* impl_;
};

void setAppDevice(EndpointRouter& r, const std::wstring& exe, const std::wstring& deviceId);
