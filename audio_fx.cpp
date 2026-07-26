#include "audio_fx.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr float kCtrlHz[kEqControls] = {
    20.f,
    112.468f,   // even steps on a log axis
    632.456f,
    3556.56f,
    20000.f
};

float dbToLin(float db) {
    return powf(10.f, db / 20.f);
}

void eqControlFreqs(float out[kEqControls]) {
    for (int i = 0; i < kEqControls; ++i) out[i] = kCtrlHz[i];
}

// smooth curve between the knobs (not straight lines)
static float catmullRom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.f * p1) +
                   (-p0 + p2) * t +
                   (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                   (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3);
}

float eqInterpControls(float freqHz, const float ctrl[kEqControls]) {
    if (freqHz <= kCtrlHz[0]) return ctrl[0];
    if (freqHz >= kCtrlHz[kEqControls - 1]) return ctrl[kEqControls - 1];

    int i = 0;
    while (i < kEqControls - 2 && freqHz > kCtrlHz[i + 1]) ++i;

    const float a = logf(kCtrlHz[i]);
    const float b = logf(kCtrlHz[i + 1]);
    const float t = (logf(freqHz) - a) / (b - a);

    const float p0 = ctrl[(std::max)(0, i - 1)];
    const float p1 = ctrl[i];
    const float p2 = ctrl[i + 1];
    const float p3 = ctrl[(std::min)(kEqControls - 1, i + 2)];
    return catmullRom(p0, p1, p2, p3, t);
}

float eqCurveDb(float freqHz, float /*sampleRate*/,
                float subDb, float lowDb, float midDb, float highDb, float airDb) {
    const float ctrl[kEqControls] = { subDb, lowDb, midDb, highDb, airDb };
    if (freqHz < 1.f) freqHz = 1.f;
    return eqInterpControls(freqHz, ctrl);
}

void Biquad::setPeaking(float sr, float freq, float q, float gainDb) {
    const float A = powf(10.f, gainDb / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / sr;
    const float alpha = sinf(w0) / (2.f * q);
    const float cosw = cosf(w0);
    const float a0 = 1.f + alpha / A;
    b0 = (1.f + alpha * A) / a0;
    b1 = (-2.f * cosw) / a0;
    b2 = (1.f - alpha * A) / a0;
    a1 = (-2.f * cosw) / a0;
    a2 = (1.f - alpha / A) / a0;
}

void Biquad::setLowShelf(float sr, float freq, float gainDb) {
    const float A = powf(10.f, gainDb / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / sr;
    const float cosw = cosf(w0);
    const float sinw = sinf(w0);
    const float alpha = sinw / 2.f * sqrtf((A + 1.f / A) * (1.f / 0.707f - 1.f) + 2.f);
    const float twoSqrtAalpha = 2.f * sqrtf(A) * alpha;
    const float a0 = (A + 1.f) + (A - 1.f) * cosw + twoSqrtAalpha;
    b0 = (A * ((A + 1.f) - (A - 1.f) * cosw + twoSqrtAalpha)) / a0;
    b1 = (2.f * A * ((A - 1.f) - (A + 1.f) * cosw)) / a0;
    b2 = (A * ((A + 1.f) - (A - 1.f) * cosw - twoSqrtAalpha)) / a0;
    a1 = (-2.f * ((A - 1.f) + (A + 1.f) * cosw)) / a0;
    a2 = ((A + 1.f) + (A - 1.f) * cosw - twoSqrtAalpha) / a0;
}

void Biquad::setHighShelf(float sr, float freq, float gainDb) {
    const float A = powf(10.f, gainDb / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / sr;
    const float cosw = cosf(w0);
    const float sinw = sinf(w0);
    const float alpha = sinw / 2.f * sqrtf((A + 1.f / A) * (1.f / 0.707f - 1.f) + 2.f);
    const float twoSqrtAalpha = 2.f * sqrtf(A) * alpha;
    const float a0 = (A + 1.f) - (A - 1.f) * cosw + twoSqrtAalpha;
    b0 = (A * ((A + 1.f) + (A - 1.f) * cosw + twoSqrtAalpha)) / a0;
    b1 = (-2.f * A * ((A - 1.f) + (A + 1.f) * cosw)) / a0;
    b2 = (A * ((A + 1.f) + (A - 1.f) * cosw - twoSqrtAalpha)) / a0;
    a1 = (2.f * ((A - 1.f) - (A + 1.f) * cosw)) / a0;
    a2 = ((A + 1.f) - (A - 1.f) * cosw - twoSqrtAalpha) / a0;
}

float Biquad::process(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

static void ctrlFromStep(const FxStep& s, float ctrl[kEqControls]) {
    ctrl[0] = s.eqSubDb;
    ctrl[1] = s.eqLowDb;
    ctrl[2] = s.eqMidDb;
    ctrl[3] = s.eqHighDb;
    ctrl[4] = s.eqAirDb;
}

static bool ctrlChanged(const float a[kEqControls], const float b[kEqControls]) {
    for (int i = 0; i < kEqControls; ++i)
        if (fabsf(a[i] - b[i]) > 0.01f) return true;
    return false;
}

// overall gain = average of knobs; peaks only do the shape on top
static void rebuildEq(FxChain::EqState& st, float sr, const float ctrl[kEqControls], bool resetZ) {
    float mean = 0.f;
    for (int i = 0; i < kEqControls; ++i) mean += ctrl[i];
    mean /= (float)kEqControls;
    st.meanDb = mean;
    st.meanLin = dbToLin(mean);
    memcpy(st.ctrl, ctrl, sizeof(float) * kEqControls);

    // sprinkle peaking filters across the spectrum to approx the curve
    constexpr float kQ = 1.15f;
    for (int i = 0; i < kEqInternal; ++i) {
        const float t = i / (float)(kEqInternal - 1);
        const float freq = 20.f * powf(1000.f, t);
        const float delta = eqInterpControls(freq, ctrl) - mean;
        st.L[i].setPeaking(sr, freq, kQ, delta);
        st.R[i].setPeaking(sr, freq, kQ, delta);
        if (resetZ) {
            st.L[i].reset();
            st.R[i].reset();
        }
    }
}

void FxChain::prepare(float sr) {
    std::lock_guard lock(mu);
    sampleRate = sr;
    eqStates.clear();
    for (auto& s : steps) {
        if (s.kind != FxKind::Eq) continue;
        EqState st;
        float ctrl[kEqControls];
        ctrlFromStep(s, ctrl);
        rebuildEq(st, sr, ctrl, true);
        eqStates.push_back(st);
    }
}

void FxChain::setSteps(std::vector<FxStep> next) {
    std::lock_guard lock(mu);
    steps = std::move(next);

    int eqCount = 0;
    for (auto& s : steps) if (s.kind == FxKind::Eq) ++eqCount;
    if ((int)eqStates.size() != eqCount) {
        eqStates.assign(eqCount, EqState{});
        int i = 0;
        for (auto& s : steps) {
            if (s.kind != FxKind::Eq) continue;
            float ctrl[kEqControls];
            ctrlFromStep(s, ctrl);
            rebuildEq(eqStates[i++], sampleRate, ctrl, true);
        }
        return;
    }

    int i = 0;
    for (auto& s : steps) {
        if (s.kind != FxKind::Eq) continue;
        auto& st = eqStates[i++];
        float ctrl[kEqControls];
        ctrlFromStep(s, ctrl);
        if (ctrlChanged(st.ctrl, ctrl))
            rebuildEq(st, sampleRate, ctrl, false);
    }
}

void FxChain::process(int16_t* interleaved, int frames, int channels) {
    if (!interleaved || frames <= 0) return;
    std::lock_guard lock(mu);
    if (steps.empty()) return;

    int eqIdxCheck = 0;
    for (auto& step : steps) {
        if (step.kind != FxKind::Eq) continue;
        if (eqIdxCheck >= (int)eqStates.size()) break;
        auto& st = eqStates[eqIdxCheck++];
        float ctrl[kEqControls];
        ctrlFromStep(step, ctrl);
        if (ctrlChanged(st.ctrl, ctrl))
            rebuildEq(st, sampleRate, ctrl, false);
    }

    int eqIdx = 0;
    for (auto& step : steps) {
        if (step.kind == FxKind::Gain) {
            const float g = dbToLin(step.gainDb);
            if (fabsf(g - 1.f) < 0.001f) continue;
            const int n = frames * channels;
            for (int i = 0; i < n; ++i) {
                float v = interleaved[i] * g;
                if (v > 32767.f) v = 32767.f;
                if (v < -32768.f) v = -32768.f;
                interleaved[i] = (int16_t)v;
            }
        } else if (step.kind == FxKind::Eq && eqIdx < (int)eqStates.size()) {
            auto& st = eqStates[eqIdx++];
            bool flatShape = true;
            for (int i = 0; i < kEqControls; ++i) {
                if (fabsf(st.ctrl[i] - st.meanDb) > 0.05f) { flatShape = false; break; }
            }
            for (int f = 0; f < frames; ++f) {
                float L = interleaved[f * channels + 0] * st.meanLin;
                if (!flatShape) {
                    for (int i = 0; i < kEqInternal; ++i)
                        L = st.L[i].process(L);
                }
                if (L > 32767.f) L = 32767.f;
                if (L < -32768.f) L = -32768.f;
                interleaved[f * channels + 0] = (int16_t)L;
                if (channels > 1) {
                    float R = interleaved[f * channels + 1] * st.meanLin;
                    if (!flatShape) {
                        for (int i = 0; i < kEqInternal; ++i)
                            R = st.R[i].process(R);
                    }
                    if (R > 32767.f) R = 32767.f;
                    if (R < -32768.f) R = -32768.f;
                    interleaved[f * channels + 1] = (int16_t)R;
                }
            }
        }
    }
}
