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

static float linToDb(float lin) {
    if (lin < 1e-8f) lin = 1e-8f;
    return 20.f * log10f(lin);
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
    b2 = (A * ((A + 1.f) - (A - 1.f) * cosw - twoSqrtAalpha)) / a0;
    a1 = (2.f * ((A - 1.f) - (A + 1.f) * cosw)) / a0;
    a2 = ((A + 1.f) - (A - 1.f) * cosw - twoSqrtAalpha) / a0;
}

void Biquad::setHighPass(float sr, float freq, float q) {
    const float w0 = 2.f * (float)M_PI * freq / sr;
    const float cosw = cosf(w0);
    const float alpha = sinf(w0) / (2.f * q);
    const float a0 = 1.f + alpha;
    b0 = ((1.f + cosw) * 0.5f) / a0;
    b1 = (-(1.f + cosw)) / a0;
    b2 = ((1.f + cosw) * 0.5f) / a0;
    a1 = (-2.f * cosw) / a0;
    a2 = (1.f - alpha) / a0;
}

void Biquad::setLowPass(float sr, float freq, float q) {
    const float w0 = 2.f * (float)M_PI * freq / sr;
    const float cosw = cosf(w0);
    const float alpha = sinf(w0) / (2.f * q);
    const float a0 = 1.f + alpha;
    b0 = ((1.f - cosw) * 0.5f) / a0;
    b1 = (1.f - cosw) / a0;
    b2 = ((1.f - cosw) * 0.5f) / a0;
    a1 = (-2.f * cosw) / a0;
    a2 = (1.f - alpha) / a0;
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

static void applyCompParams(FxChain::CompState& st, const FxStep& s) {
    st.thresholdDb = s.compThresholdDb;
    st.ratio = s.compRatio;
    st.attackMs = s.compAttackMs;
    st.releaseMs = s.compReleaseMs;
    st.makeupDb = s.compMakeupDb;
}

static bool compParamsChanged(const FxChain::CompState& st, const FxStep& s) {
    return fabsf(st.thresholdDb - s.compThresholdDb) > 0.01f
        || fabsf(st.ratio - s.compRatio) > 0.01f
        || fabsf(st.attackMs - s.compAttackMs) > 0.01f
        || fabsf(st.releaseMs - s.compReleaseMs) > 0.01f
        || fabsf(st.makeupDb - s.compMakeupDb) > 0.01f;
}

static void rebuildHp(FxChain::FilterState& st, float sr, float hz, bool resetZ) {
    st.hz = hz;
    if (hz <= 0.f) {
        st.L = Biquad{};
        st.R = Biquad{};
        return;
    }
    constexpr float kQ = 0.707f;
    st.L.setHighPass(sr, hz, kQ);
    st.R.setHighPass(sr, hz, kQ);
    if (resetZ) {
        st.L.reset();
        st.R.reset();
    }
}

static void rebuildLp(FxChain::FilterState& st, float sr, float hz, bool resetZ) {
    st.hz = hz;
    if (hz <= 0.f) {
        st.L = Biquad{};
        st.R = Biquad{};
        return;
    }
    constexpr float kQ = 0.707f;
    st.L.setLowPass(sr, hz, kQ);
    st.R.setLowPass(sr, hz, kQ);
    if (resetZ) {
        st.L.reset();
        st.R.reset();
    }
}

static void applyLimitParams(FxChain::LimitState& st, const FxStep& s) {
    st.thresholdDb = s.limitThresholdDb;
    st.releaseMs = s.limitReleaseMs;
}

static bool limitParamsChanged(const FxChain::LimitState& st, const FxStep& s) {
    return fabsf(st.thresholdDb - s.limitThresholdDb) > 0.01f
        || fabsf(st.releaseMs - s.limitReleaseMs) > 0.01f;
}

static void applyGateParams(FxChain::GateState& st, const FxStep& s) {
    st.thresholdDb = s.gateThresholdDb;
    st.attackMs = s.gateAttackMs;
    st.releaseMs = s.gateReleaseMs;
    st.rangeDb = s.gateRangeDb;
}

static bool gateParamsChanged(const FxChain::GateState& st, const FxStep& s) {
    return fabsf(st.thresholdDb - s.gateThresholdDb) > 0.01f
        || fabsf(st.attackMs - s.gateAttackMs) > 0.01f
        || fabsf(st.releaseMs - s.gateReleaseMs) > 0.01f
        || fabsf(st.rangeDb - s.gateRangeDb) > 0.01f;
}

static void clamp16(float& v) {
    if (v > 32767.f) v = 32767.f;
    if (v < -32768.f) v = -32768.f;
}

static void rebuildStatefulSteps(FxChain& chain, bool resetZ) {
    int eqCount = 0, compCount = 0, hpCount = 0, lpCount = 0;
    int limitCount = 0, gateCount = 0, waveCount = 0;
    for (auto& s : chain.steps) {
        if (s.kind == FxKind::Eq) ++eqCount;
        else if (s.kind == FxKind::Comp) ++compCount;
        else if (s.kind == FxKind::HighPass) ++hpCount;
        else if (s.kind == FxKind::LowPass) ++lpCount;
        else if (s.kind == FxKind::Limit) ++limitCount;
        else if (s.kind == FxKind::Gate) ++gateCount;
        else if (s.kind == FxKind::Waveform) ++waveCount;
    }

    auto resize = [&](auto& vec, int n) {
        if ((int)vec.size() != n) {
            vec.assign(n, {});
            resetZ = true;
        }
    };
    resize(chain.eqStates, eqCount);
    resize(chain.compStates, compCount);
    resize(chain.hpStates, hpCount);
    resize(chain.lpStates, lpCount);
    resize(chain.limitStates, limitCount);
    resize(chain.gateStates, gateCount);
    resize(chain.waveStates, waveCount);

    int eqi = 0, compi = 0, hpi = 0, lpi = 0, limi = 0, gatei = 0;
    for (auto& s : chain.steps) {
        if (s.kind == FxKind::Eq) {
            auto& st = chain.eqStates[eqi++];
            float ctrl[kEqControls];
            ctrlFromStep(s, ctrl);
            if (resetZ || ctrlChanged(st.ctrl, ctrl))
                rebuildEq(st, chain.sampleRate, ctrl, resetZ);
        } else if (s.kind == FxKind::Comp) {
            auto& st = chain.compStates[compi++];
            if (resetZ || compParamsChanged(st, s)) {
                applyCompParams(st, s);
                if (resetZ) st.env = 0.f;
            }
        } else if (s.kind == FxKind::HighPass) {
            auto& st = chain.hpStates[hpi++];
            if (resetZ || fabsf(st.hz - s.hpHz) > 0.1f)
                rebuildHp(st, chain.sampleRate, s.hpHz, resetZ);
        } else if (s.kind == FxKind::LowPass) {
            auto& st = chain.lpStates[lpi++];
            if (resetZ || fabsf(st.hz - s.lpHz) > 0.1f)
                rebuildLp(st, chain.sampleRate, s.lpHz, resetZ);
        } else if (s.kind == FxKind::Limit) {
            auto& st = chain.limitStates[limi++];
            if (resetZ || limitParamsChanged(st, s)) {
                applyLimitParams(st, s);
                if (resetZ) st.env = 0.f;
            }
        } else if (s.kind == FxKind::Gate) {
            auto& st = chain.gateStates[gatei++];
            if (resetZ || gateParamsChanged(st, s)) {
                applyGateParams(st, s);
                if (resetZ) { st.env = 0.f; st.gain = 1.f; }
            }
        }
    }
}

void FxChain::prepare(float sr) {
    std::lock_guard lock(mu);
    sampleRate = sr;
    eqStates.clear();
    compStates.clear();
    hpStates.clear();
    lpStates.clear();
    limitStates.clear();
    gateStates.clear();
    waveStates.clear();
    rebuildStatefulSteps(*this, true);
}

void FxChain::setSteps(std::vector<FxStep> next) {
    std::lock_guard lock(mu);
    steps = std::move(next);
    rebuildStatefulSteps(*this, false);
}

static void processGain(FxStep& step, int16_t* interleaved, int frames, int channels) {
    const float g = dbToLin(step.gainDb);
    if (fabsf(g - 1.f) < 0.001f) return;
    const int n = frames * channels;
    for (int i = 0; i < n; ++i) {
        float v = interleaved[i] * g;
        clamp16(v);
        interleaved[i] = (int16_t)v;
    }
}

static void processEq(FxChain::EqState& st, int16_t* interleaved, int frames, int channels) {
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
        clamp16(L);
        interleaved[f * channels + 0] = (int16_t)L;
        if (channels > 1) {
            float R = interleaved[f * channels + 1] * st.meanLin;
            if (!flatShape) {
                for (int i = 0; i < kEqInternal; ++i)
                    R = st.R[i].process(R);
            }
            clamp16(R);
            interleaved[f * channels + 1] = (int16_t)R;
        }
    }
}

static void processComp(FxChain::CompState& st, const FxStep& step, float sr,
                        int16_t* interleaved, int frames, int channels) {
    if (compParamsChanged(st, step))
        applyCompParams(st, step);

    const float thresh = st.thresholdDb;
    const float ratio = (st.ratio < 1.f) ? 1.f : st.ratio;
    const float makeup = dbToLin(st.makeupDb);
    const float atkCoef = expf(-1.f / ((std::max)(0.1f, st.attackMs) * 0.001f * sr));
    const float relCoef = expf(-1.f / ((std::max)(1.f, st.releaseMs) * 0.001f * sr));

    for (int f = 0; f < frames; ++f) {
        float peak = fabsf((float)interleaved[f * channels + 0]);
        if (channels > 1) {
            const float r = fabsf((float)interleaved[f * channels + 1]);
            if (r > peak) peak = r;
        }
        const float level = peak / 32768.f;
        if (level > st.env)
            st.env = atkCoef * st.env + (1.f - atkCoef) * level;
        else
            st.env = relCoef * st.env + (1.f - relCoef) * level;

        const float envDb = linToDb(st.env);
        float grDb = 0.f;
        if (envDb > thresh)
            grDb = (thresh - envDb) * (1.f - 1.f / ratio);
        const float g = dbToLin(grDb) * makeup;

        for (int c = 0; c < channels; ++c) {
            float v = interleaved[f * channels + c] * g;
            clamp16(v);
            interleaved[f * channels + c] = (int16_t)v;
        }
    }
}

static void processPan(const FxStep& step, int16_t* interleaved, int frames, int channels) {
    if (channels < 2) return;
    float p = step.pan;
    if (p < -1.f) p = -1.f;
    if (p > 1.f) p = 1.f;
    if (fabsf(p) < 0.001f) return;

    // constant-power: pan -1..+1 → angle 0..pi/2
    const float angle = (p + 1.f) * 0.25f * (float)M_PI;
    const float gL = cosf(angle);
    const float gR = sinf(angle);
    for (int f = 0; f < frames; ++f) {
        float L = interleaved[f * channels + 0] * gL;
        float R = interleaved[f * channels + 1] * gR;
        clamp16(L);
        clamp16(R);
        interleaved[f * channels + 0] = (int16_t)L;
        interleaved[f * channels + 1] = (int16_t)R;
    }
}

static void processHp(FxChain::FilterState& st, const FxStep& step, float sr,
                      int16_t* interleaved, int frames, int channels) {
    if (fabsf(st.hz - step.hpHz) > 0.1f)
        rebuildHp(st, sr, step.hpHz, false);
    if (step.hpHz <= 0.f) return;

    for (int f = 0; f < frames; ++f) {
        float L = st.L.process((float)interleaved[f * channels + 0]);
        clamp16(L);
        interleaved[f * channels + 0] = (int16_t)L;
        if (channels > 1) {
            float R = st.R.process((float)interleaved[f * channels + 1]);
            clamp16(R);
            interleaved[f * channels + 1] = (int16_t)R;
        }
    }
}

static void processLp(FxChain::FilterState& st, const FxStep& step, float sr,
                      int16_t* interleaved, int frames, int channels) {
    if (fabsf(st.hz - step.lpHz) > 0.1f)
        rebuildLp(st, sr, step.lpHz, false);
    if (step.lpHz <= 0.f) return;

    for (int f = 0; f < frames; ++f) {
        float L = st.L.process((float)interleaved[f * channels + 0]);
        clamp16(L);
        interleaved[f * channels + 0] = (int16_t)L;
        if (channels > 1) {
            float R = st.R.process((float)interleaved[f * channels + 1]);
            clamp16(R);
            interleaved[f * channels + 1] = (int16_t)R;
        }
    }
}

static void processLimit(FxChain::LimitState& st, const FxStep& step, float sr,
                         int16_t* interleaved, int frames, int channels) {
    if (limitParamsChanged(st, step))
        applyLimitParams(st, step);

    const float thresh = st.thresholdDb;
    // near-instant attack, tunable release
    const float atkCoef = expf(-1.f / (0.2f * 0.001f * sr));
    const float relCoef = expf(-1.f / ((std::max)(1.f, st.releaseMs) * 0.001f * sr));

    for (int f = 0; f < frames; ++f) {
        float peak = fabsf((float)interleaved[f * channels + 0]);
        if (channels > 1) {
            const float r = fabsf((float)interleaved[f * channels + 1]);
            if (r > peak) peak = r;
        }
        const float level = peak / 32768.f;
        if (level > st.env)
            st.env = atkCoef * st.env + (1.f - atkCoef) * level;
        else
            st.env = relCoef * st.env + (1.f - relCoef) * level;

        const float envDb = linToDb(st.env);
        float grDb = 0.f;
        if (envDb > thresh)
            grDb = thresh - envDb; // infinite ratio
        const float g = dbToLin(grDb);

        for (int c = 0; c < channels; ++c) {
            float v = interleaved[f * channels + c] * g;
            clamp16(v);
            interleaved[f * channels + c] = (int16_t)v;
        }
    }
}

static void processGate(FxChain::GateState& st, const FxStep& step, float sr,
                        int16_t* interleaved, int frames, int channels) {
    if (gateParamsChanged(st, step))
        applyGateParams(st, step);

    const float thresh = st.thresholdDb;
    const float closed = dbToLin(st.rangeDb);
    const float atkCoef = expf(-1.f / ((std::max)(0.1f, st.attackMs) * 0.001f * sr));
    const float relCoef = expf(-1.f / ((std::max)(1.f, st.releaseMs) * 0.001f * sr));
    // envelope follow
    const float envAtk = expf(-1.f / (1.f * 0.001f * sr));
    const float envRel = expf(-1.f / (20.f * 0.001f * sr));

    for (int f = 0; f < frames; ++f) {
        float peak = fabsf((float)interleaved[f * channels + 0]);
        if (channels > 1) {
            const float r = fabsf((float)interleaved[f * channels + 1]);
            if (r > peak) peak = r;
        }
        const float level = peak / 32768.f;
        if (level > st.env)
            st.env = envAtk * st.env + (1.f - envAtk) * level;
        else
            st.env = envRel * st.env + (1.f - envRel) * level;

        const float target = (linToDb(st.env) >= thresh) ? 1.f : closed;
        if (target > st.gain)
            st.gain = atkCoef * st.gain + (1.f - atkCoef) * target;
        else
            st.gain = relCoef * st.gain + (1.f - relCoef) * target;

        for (int c = 0; c < channels; ++c) {
            float v = interleaved[f * channels + c] * st.gain;
            clamp16(v);
            interleaved[f * channels + c] = (int16_t)v;
        }
    }
}

static void processWave(FxChain::WaveState& st, int16_t* interleaved, int frames, int channels) {
    if (frames <= 0) return;
    for (int i = 0; i < kWaveCapture; ++i) {
        int f = (i * frames) / kWaveCapture;
        if (f >= frames) f = frames - 1;
        float v = interleaved[f * channels + 0] / 32768.f;
        if (channels > 1)
            v = 0.5f * (v + interleaved[f * channels + 1] / 32768.f);
        st.samples[i] = v;
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

    int eqIdx = 0, compIdx = 0, hpIdx = 0, lpIdx = 0;
    int limIdx = 0, gateIdx = 0, waveIdx = 0;
    for (auto& step : steps) {
        if (step.kind == FxKind::Gain) {
            processGain(step, interleaved, frames, channels);
        } else if (step.kind == FxKind::Eq && eqIdx < (int)eqStates.size()) {
            processEq(eqStates[eqIdx++], interleaved, frames, channels);
        } else if (step.kind == FxKind::Comp && compIdx < (int)compStates.size()) {
            processComp(compStates[compIdx++], step, sampleRate, interleaved, frames, channels);
        } else if (step.kind == FxKind::Pan) {
            processPan(step, interleaved, frames, channels);
        } else if (step.kind == FxKind::HighPass && hpIdx < (int)hpStates.size()) {
            processHp(hpStates[hpIdx++], step, sampleRate, interleaved, frames, channels);
        } else if (step.kind == FxKind::LowPass && lpIdx < (int)lpStates.size()) {
            processLp(lpStates[lpIdx++], step, sampleRate, interleaved, frames, channels);
        } else if (step.kind == FxKind::Limit && limIdx < (int)limitStates.size()) {
            processLimit(limitStates[limIdx++], step, sampleRate, interleaved, frames, channels);
        } else if (step.kind == FxKind::Gate && gateIdx < (int)gateStates.size()) {
            processGate(gateStates[gateIdx++], step, sampleRate, interleaved, frames, channels);
        } else if (step.kind == FxKind::Waveform && waveIdx < (int)waveStates.size()) {
            processWave(waveStates[waveIdx++], interleaved, frames, channels);
        }
    }
}
