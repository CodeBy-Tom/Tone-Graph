#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

enum class FxKind { Gain, Eq };

struct FxStep {
    FxKind kind = FxKind::Gain;
    float gainDb = 0.f;
    // five knobs on the EQ curve
    float eqSubDb = 0.f;
    float eqLowDb = 0.f;
    float eqMidDb = 0.f;
    float eqHighDb = 0.f;
    float eqAirDb = 0.f;
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    void setPeaking(float sr, float freq, float q, float gainDb);
    void setLowShelf(float sr, float freq, float gainDb);
    void setHighShelf(float sr, float freq, float gainDb);
    float process(float x);
    void reset() { z1 = z2 = 0; }
};

constexpr int kEqControls = 5;
constexpr int kEqInternal = 12;

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
    std::vector<EqState> eqStates;
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
