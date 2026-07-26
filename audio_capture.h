#pragma once

#include "audio_fx.h"
#include <windows.h>
#include <string>

bool streamLoopback(DWORD pid, const std::wstring& outId, FxChain* fx = nullptr);
bool streamCaptureDevice(const std::wstring& capId, const std::wstring& outId, FxChain* fx = nullptr);
