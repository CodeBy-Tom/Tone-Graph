#pragma once

#include "common.h"
#include "audio_fx.h"
#include <string>

WAVEFORMATEX pcm48();
bool pumpAudio(IAudioClient* capClient, IAudioCaptureClient* cap, HANDLE ready,
               const WAVEFORMATEX& fmt, const std::wstring& outId, FxChain* fx);
