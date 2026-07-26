#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

enum class FxKind { Gain, Eq, Comp, Pan, HighPass, Waveform, LowPass, Limit, Gate };

enum class FxChannel : int {
    Both = 0,
    Left = 1,
    Right = 2,
};

struct FxStep {
    FxKind kind = FxKind::Gain;
    FxChannel channel = FxChannel::Both;
    float gainDb = 0.f;
    // five knobs on the EQ curve
    float eqSubDb = 0.f;
    float eqLowDb = 0.f;
    float eqMidDb = 0.f;
    float eqHighDb = 0.f;
    float eqAirDb = 0.f;
    // compressor
    float compThresholdDb = -18.f;
    float compRatio = 4.f;
    float compAttackMs = 10.f;
    float compReleaseMs = 100.f;
    float compMakeupDb = 0.f;
    // pan: -1 = full left, 0 = center, +1 = full right
    float pan = 0.f;
    // high-pass / low-pass cutoff Hz; 0 = bypass
    float hpHz = 0.f;
    float lpHz = 0.f;
    // limiter
    float limitThresholdDb = -1.f;
    float limitReleaseMs = 50.f;
    // gate
    float gateThresholdDb = -40.f;
    float gateAttackMs = 5.f;
    float gateReleaseMs = 100.f;
    float gateRangeDb = -60.f; // gain when closed
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    void setPeaking(float sr, float freq, float q, float gainDb);
    void setLowShelf(float sr, float freq, float gainDb);
    void setHighShelf(float sr, float freq, float gainDb);
    void setHighPass(float sr, float freq, float q);
    void setLowPass(float sr, float freq, float q);
    float process(float x);
    void reset() { z1 = z2 = 0; }
};

constexpr int kEqControls = 5;
constexpr int kEqInternal = 12;
constexpr int kWaveCapture = 128;

struct FxChain {
    std::mutex mu;
    std::vector<FxStep> steps;
    struct EqState {
        Biquad L[kEqInternal]{};
        Biquad R[kEqInternal]{};
        float ctrl[kEqControls]{ 1e9f, 1e9f, 1e9f, 1e9f, 1e9f };
        float meanDb = 0.f;
        float meanLin = 1.f;
    };
    struct CompState {
        float env = 0.f;
        float thresholdDb = 1e9f;
        float ratio = 0.f;
        float attackMs = 0.f;
        float releaseMs = 0.f;
        float makeupDb = 0.f;
    };
    struct FilterState {
        Biquad L{};
        Biquad R{};
        float hz = -1.f;
    };
    struct LimitState {
        float env = 0.f;
        float thresholdDb = 1e9f;
        float releaseMs = 0.f;
    };
    struct GateState {
        float env = 0.f;
        float gain = 1.f;
        float thresholdDb = 1e9f;
        float attackMs = 0.f;
        float releaseMs = 0.f;
        float rangeDb = 0.f;
    };
    struct WaveState {
        float samples[kWaveCapture]{};
    };
    std::vector<EqState> eqStates;
    std::vector<CompState> compStates;
    std::vector<FilterState> hpStates;
    std::vector<FilterState> lpStates;
    std::vector<LimitState> limitStates;
    std::vector<GateState> gateStates;
    std::vector<WaveState> waveStates;
    float sampleRate = 48000.f;

    void prepare(float sr);
    void setSteps(std::vector<FxStep> next);
    void process(int16_t* interleaved, int frames, int channels);
};

float dbToLin(float db);

// what the UI draws for the EQ
float eqCurveDb(float freqHz, float sampleRate,
                float subDb, float lowDb, float midDb, float highDb, float airDb);

void eqControlFreqs(float out[kEqControls]);
float eqInterpControls(float freqHz, const float ctrl[kEqControls]);
