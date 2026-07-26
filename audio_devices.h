#pragma once

#include "app_state.h"
#include <vector>

bool isCable(const std::wstring& name);
std::vector<AudioApp> listApps();
std::vector<OutDevice> listOutputs();
std::wstring findCaptureId(const wchar_t* needle);
int muteAppOnDevice(const std::wstring& exe, const std::wstring& devName, bool mute);
