// Lfo.h — the two modulation LFOs, framework-agnostic and header-only.
//
// An LFO produces a value in -1..1. A knob it is assigned to moves around
// its own setting by `depth` (also -1..1) of the knob's full travel, in the
// knob's normalised 0..1 space -- so a frequency knob with a log taper is
// swept evenly in octaves, the same way turning it by hand would.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rumble {

enum class LfoShape { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
constexpr int kNumLfoShapes = 7;
constexpr const char* kLfoShapeNames[kNumLfoShapes] = {
    "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random"
};

// Tempo-synced cycle lengths, in quarter notes, longest first.
struct LfoDivision { const char* name; double beats; };
constexpr LfoDivision kLfoDivisions[] = {
    { "8 bars", 32.0 }, { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1/1", 4.0 },
    { "1/2", 2.0 },     { "1/2T", 4.0 / 3.0 }, { "1/4D", 1.5 }, { "1/4", 1.0 },
    { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 }, { "1/8", 0.5 },   { "1/8T", 1.0 / 3.0 },
    { "1/16", 0.25 },   { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },
};
constexpr int kNumLfoDivisions = (int) (sizeof(kLfoDivisions) / sizeof(kLfoDivisions[0]));
constexpr int kDefaultLfoDivision = 3;   // 1/1: one sweep per bar

// The value of `shape` at `phase` (0..1). `rnd` and `prevRnd` are the random
// values for this cycle and the previous one, used by the random shapes.
inline float lfoShapeValue(LfoShape shape, double phase, float rnd, float prevRnd) {
    const float p = (float) phase;
    switch (shape) {
        case LfoShape::Sine:     return std::sin(2.0f * 3.14159265358979f * p);
        case LfoShape::Triangle: // starts at 0 and rises, like the sine
            return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        case LfoShape::SawUp:    return 2.0f * p - 1.0f;
        case LfoShape::SawDown:  return 1.0f - 2.0f * p;
        case LfoShape::Square:   return p < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleHold: return rnd;
        case LfoShape::SmoothRandom: {
            // Cosine glide from the previous random value to this one.
            const float t = 0.5f - 0.5f * std::cos(3.14159265358979f * p);
            return prevRnd + t * (rnd - prevRnd);
        }
    }
    return 0.0f;
}

// A repeatable random value in -1..1 for cycle `n`. Hashing the cycle number
// (rather than running a generator) means a synced random LFO plays the same
// pattern every time the song plays through the same bars.
inline float lfoRandom(std::int64_t n, std::uint32_t seed) {
    std::uint64_t x = (std::uint64_t) n * 0x9E3779B97F4A7C15ull + seed;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (float) ((double) (x >> 11) / 9007199254740992.0) * 2.0f - 1.0f;
}

class Lfo {
public:
    explicit Lfo(std::uint32_t randomSeed = 1) : seed(randomSeed) {}

    void prepare(double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); }
    void reset() { phase = 0.0; cycle = 0; }

    struct Settings {
        LfoShape shape = LfoShape::Sine;
        double rateHz = 1.0;        // used when not synced
        bool sync = false;
        double cycleBeats = 4.0;    // used when synced
    };

    // The value at the current position, then advance by `numSamples`.
    // When synced and the host reports a playing position, the phase is
    // locked to the bar (`ppq` is the position at this block's start);
    // otherwise it runs freely, at the tempo-derived rate if synced.
    float next(const Settings& s, int numSamples, bool ppqValid, double ppq, double bpm) {
        if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;
        const double beats = std::max(1.0e-3, s.cycleBeats);

        if (s.sync && ppqValid && std::isfinite(ppq)) {
            const double pos = ppq / beats;
            const double whole = std::floor(pos);
            phase = pos - whole;
            cycle = (std::int64_t) whole;
        }

        const float value = lfoShapeValue(s.shape, phase,
                                          lfoRandom(cycle, seed), lfoRandom(cycle - 1, seed));
        lastValue = value;

        // Advance the free-running phase (also how sync carries on while
        // the transport is stopped).
        const double hz = s.sync ? bpm / 60.0 / beats : std::clamp(s.rateHz, 0.0, 1000.0);
        phase += hz * (double) numSamples / sr;
        if (phase >= 1.0) {
            const double wraps = std::floor(phase);
            phase -= wraps;
            cycle += (std::int64_t) wraps;
        }
        return value;
    }

    double getPhase() const { return phase; }
    float getLastValue() const { return lastValue; }

private:
    double sr = 48000.0;
    double phase = 0.0;
    std::int64_t cycle = 0;
    std::uint32_t seed;
    float lastValue = 0.0f;
};

// A knob's setting moved by the LFOs, in normalised 0..1 space.
inline float modulateNormalised(float base, const float* depths, const float* lfoValues, int count) {
    float v = base;
    for (int i = 0; i < count; ++i) v += depths[i] * lfoValues[i];
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace rumble
