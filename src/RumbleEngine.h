// RumbleEngine.h — framework-agnostic DSP core for the Rumble reverb.
//
// Signal flow (per sample):
//   in -> hp/lp shaping -> drive -> predelay (optionally tempo-synced, with
//   swing) -> allpass diffusion -> FDN reverb (8 lines, Hadamard mix, damped +
//   low-cut feedback, modulated delay taps) -> tone -> resonant filter ->
//   tail drive -> enhancer -> EQ -> echo delay -> duck (envelope of the
//   input or an external sidechain) -> width / mono-below -> out
//
// The output is always fully wet. This plugin replaces a kick with the
// rumble it generates rather than blending the two, so there is no dry/wet
// control; balance the rumble against the kick with a send or a duplicate
// track, where the DAW's own faders do the job properly.
//
// The duck stage is what turns a plain reverb tail into a techno rumble: the
// tail is pumped down by the kick that feeds it, so it breathes in the gaps.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rumble {

constexpr float kPi = 3.14159265358979323846f;

// Reference frequency at which the Decay knob is specified, and the ceiling
// on how much gain may be handed back to compensate the in-loop low cut.
constexpr float kDecayRefHz = 400.0f;
constexpr float kMaxLowCutMakeup = 1.10f;

// 1/sqrt(8): keeps the 8x8 Hadamard orthonormal.
constexpr float kHadamardScale = 0.35355339f;

// Upper bound on pre-delay, sized for a synced 1/1 at 60 BPM with full swing
// (4000 ms + 50%) plus headroom. Slower tempos at the longest divisions clamp,
// which only means the tail lands early.
constexpr float kMaxPredelayMs = 6500.0f;

// When the pre-delay time changes, the old and new times are crossfaded over
// this long instead of jumping, so automation, tempo changes and swing never
// click. In sync mode the fade is scheduled to finish *before* the grid slot
// boundary, so a kick that starts on the boundary gets one clean delay time.
constexpr float kPredelayFadeMs = 3.0f;

// How far ahead of a grid boundary the swing crossfade starts: the fade plus
// a margin, so a kick placed a little early (sample rounding, humanised
// MIDI) still gets one clean delay time.
constexpr float kSwingLookaheadMs = kPredelayFadeMs + 2.0f;

// Time constant for parameter smoothing. Filter and delay coefficients are
// recomputed every kSubBlock samples while a parameter is moving, so
// automation glides instead of stepping once per host block.
constexpr float kSmoothMs = 30.0f;

// Longest echo time: a synced 1/1 at 60 BPM. Longer settings clamp.
constexpr float kMaxEchoMs = 4000.0f;

// How quickly the echo time follows a change. Moving a delay's read head
// bends the pitch of what is already in it, like a tape delay; this keeps the
// bend short and smooth instead of a click.
constexpr float kEchoGlideMs = 80.0f;

// Feedback ceiling. With the soft clipper in the loop the repeats could not
// run away even at 1.0, but above this they stop decaying in any useful way.
constexpr float kMaxEchoFeedback = 0.95f;
constexpr int   kSubBlock = 32;

// Crossover for the enhancer's harmonic generator.
constexpr float kEnhanceBandHz = 120.0f;

// Fixed corner frequencies for the EQ shelves. These sit inside the band the
// plugin actually outputs: with Tone and Damping doing their job there is
// almost nothing above 1 kHz to shape, so a conventional 2.5 kHz "high" shelf
// would do nothing at all here.
constexpr float kEqLowHz  = 90.0f;    // weight
constexpr float kEqHighHz = 350.0f;   // edge / definition

// Largest sample magnitude accepted from the host (+18 dBFS).
constexpr float kMaxSample = 8.0f;

// Upper bound on the duck envelope; see EnvelopeFollower::process.
constexpr float kEnvelopeCeiling = 1.0f;

struct Params {
    float drive      = 2.0f;   // 1..10 pre-reverb saturation
    float tailDrive  = 2.0f;   // 1..20 post-reverb saturation: harmonics that
                               // let the rumble read on small speakers
    float predelayMs = 8.0f;   // 0..kMaxPredelayMs, used when sync is off

    // Tempo sync. When on, the pre-delay is divBeats quarter notes long, and
    // swing delays the tails that land on every second slot of that grid.
    bool  sync       = false;
    float divBeats   = 0.25f;  // length of one grid slot in quarter notes
    float swing      = 0.5f;   // 0.5 straight .. 0.75 fully swung

    float size       = 0.35f;  // 0..1 (small rooms rumble tighter)
    float decaySec   = 3.5f;   // 0.2..30 RT60
    float dampHz     = 1200.f; // feedback high-cut: kills the "air", keeps weight
    float lowCutHz   = 30.f;   // input + feedback low-cut: stops mud / DC build-up
    float toneHz     = 400.f;  // post high-cut on the wet signal
    float diffusion  = 0.70f;  // 0..1 input smearing before the network
    float modDepth   = 0.25f;  // 0..1 delay modulation (smears the metallic ring)
    float modRate    = 1.0f;   // scales the LFO bank; 1.0 = movement every ~9-23 s
    float duckAmount = 0.85f;  // 0..1 how hard the trigger ducks the tail
    float duckThreshDb = -6.0f;// trigger level that reaches full duck depth
    float duckAtkMs  = 2.0f;
    float duckRelMs  = 220.f;
    // Resonant filter across the wet signal, for sweeps. Defaults are
    // transparent so it does nothing until the user reaches for it.
    int   filterType = 0;      // index into kFilterModes
    float filterHz   = 20000.f;
    float filterQ    = 0.7f;

    // Sub-harmonic enhancer: synthesises harmonics of the low band so the
    // rumble survives on speakers that cannot reproduce the fundamental.
    float enhance    = 0.0f;   // 0..1, 0 = off

    // Three-band EQ on the wet signal. Unlike every other filter here it can
    // boost as well as cut. 0 dB everywhere is transparent.
    float eqLowDb    = 0.0f;   // low shelf
    float eqMidDb    = 0.0f;   // peaking
    float eqMidHz    = 220.f;
    float eqHighDb   = 0.0f;   // high shelf

    // Echo delay on the finished tail, before the ducker so the kick pumps
    // the repeats too. Off at delayMix = 0, which skips the stage entirely.
    // Follows `sync`: delayBeats when on, delayMs when off.
    float delayMix      = 0.0f;   // 0..1 level of the repeats
    float delayMs       = 375.0f; // 1..kMaxEchoMs
    float delayBeats    = 0.75f;  // quarter notes (0.75 = dotted 1/8)
    float delayFeedback = 0.35f;  // 0..kMaxEchoFeedback
    bool  delayPingPong = false;  // repeats alternate left / right

    float width      = 1.0f;   // 0..2 stereo width of the tail
    float monoBelowHz= 140.f;  // sub stays centred
    float outGain    = 1.0f;
};

// Host timeline for the current block. `valid` is false when the host is
// not reporting a position (transport stopped, or no playhead at all).
struct Timeline {
    bool   valid = false;
    double ppq   = 0.0;    // position at the first sample of the block
    double bpm   = 120.0;
};

// ---------------------------------------------------------------- primitives

class OnePoleLP {
public:
    void setCutoff(float hz, float sr) {
        hz = std::clamp(hz, 10.0f, sr * 0.49f);
        a = 1.0f - std::exp(-2.0f * kPi * hz / sr);
    }
    inline float process(float x) { z += a * (x - z); return z; }
    void reset() { z = 0.0f; }
private:
    float a = 0.5f, z = 0.0f;
};

class OnePoleHP {
public:
    void setCutoff(float hz, float sr) { lp.setCutoff(hz, sr); }
    inline float process(float x) { return x - lp.process(x); }
    void reset() { lp.reset(); }
private:
    OnePoleLP lp;
};

// RBJ cookbook biquad, transposed direct form II. Used for the EQ, which
// needs boost as well as cut -- every other filter here can only attenuate.
class Biquad {
public:
    enum Shape { LowShelf, Peak, HighShelf };

    void set(Shape shape, float hz, float gainDb, float q, float sr) {
        hz = std::clamp(hz, 20.0f, sr * 0.45f);
        q  = std::clamp(q, 0.1f, 10.0f);
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float sqA = std::sqrt(A);

        float b0, b1, b2, a0, a1, a2;
        switch (shape) {
            case LowShelf:
                b0 =      A * ((A + 1) - (A - 1) * cw + 2 * sqA * alpha);
                b1 =  2 * A * ((A - 1) - (A + 1) * cw);
                b2 =      A * ((A + 1) - (A - 1) * cw - 2 * sqA * alpha);
                a0 =           (A + 1) + (A - 1) * cw + 2 * sqA * alpha;
                a1 =     -2 * ((A - 1) + (A + 1) * cw);
                a2 =           (A + 1) + (A - 1) * cw - 2 * sqA * alpha;
                break;
            case HighShelf:
                b0 =      A * ((A + 1) + (A - 1) * cw + 2 * sqA * alpha);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 =      A * ((A + 1) + (A - 1) * cw - 2 * sqA * alpha);
                a0 =           (A + 1) - (A - 1) * cw + 2 * sqA * alpha;
                a1 =      2 * ((A - 1) - (A + 1) * cw);
                a2 =           (A + 1) - (A - 1) * cw - 2 * sqA * alpha;
                break;
            case Peak:
            default:
                b0 = 1 + alpha * A;
                b1 = -2 * cw;
                b2 = 1 - alpha * A;
                a0 = 1 + alpha / A;
                a1 = -2 * cw;
                a2 = 1 - alpha / A;
                break;
        }
        if (!std::isfinite(a0) || std::fabs(a0) < 1.0e-12f) { bypass(); return; }
        c0 = b0 / a0; c1 = b1 / a0; c2 = b2 / a0;
        d1 = a1 / a0; d2 = a2 / a0;
        if (!std::isfinite(c0) || !std::isfinite(c1) || !std::isfinite(c2)
            || !std::isfinite(d1) || !std::isfinite(d2)) bypass();
    }

    inline float process(float x) {
        const float y = c0 * x + z1;
        z1 = c1 * x - d1 * y + z2;
        z2 = c2 * x - d2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0f; }

private:
    void bypass() { c0 = 1.0f; c1 = c2 = d1 = d2 = 0.0f; }
    float c0 = 1.0f, c1 = 0.0f, c2 = 0.0f, d1 = 0.0f, d2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

// Topology-preserving-transform state variable filter (Zavalishin). Unlike the
// one-poles above this one resonates, and it stays stable when the cutoff is
// swept quickly -- which is the whole point of having it.
class SVF {
public:
    enum Type { LowPass = 0, BandPass, HighPass, Notch, AllPass };

    void set(float hz, float q, float sr) {
        hz = std::clamp(hz, 20.0f, sr * 0.45f);
        q  = std::clamp(q, 0.5f, 40.0f);   // cascades scale this up; see FilterCascade
        g  = std::tan(kPi * hz / sr);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline float process(float x, Type type) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        switch (type) {
            case BandPass: return v1;
            case HighPass: return x - k * v1 - v2;
            case Notch:    return x - k * v1;              // low + high
            case AllPass:  return x - 2.0f * k * v1;       // flat, phase only
            case LowPass:
            default:       return v2;
        }
    }

    void reset() { ic1 = ic2 = 0.0f; }

private:
    float g = 0.0f, k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// A cascade of up to four SVF stages, giving 12, 24 or 48 dB/oct.
//
// Cascading identical stages would multiply the resonance and drop the -3 dB
// point, so low/high pass cascades use Butterworth Q values and the user's
// Resonance scales only the final stage -- the one that produces the audible
// peak. At Resonance 0.707 the response is maximally flat by construction.
class FilterCascade {
public:
    static constexpr int kMaxStages = 4;

    void configure(SVF::Type type, int stages, float hz, float q, float sr) {
        shape = type;
        count = std::clamp(stages, 1, kMaxStages);

        static const float butter1[1] = { 0.70710678f };
        static const float butter2[2] = { 0.54119610f, 1.30656296f };
        static const float butter4[4] = { 0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f };
        const float* butter = (count == 1) ? butter1 : (count == 2 ? butter2 : butter4);

        const bool shaped = (type == SVF::LowPass || type == SVF::HighPass);
        const float resScale = std::max(q, 0.5f) / 0.70710678f;

        for (int i = 0; i < count; ++i) {
            float stageQ;
            if (!shaped) {
                stageQ = q;                                   // band pass, notch, all pass
            } else if (i == count - 1) {
                stageQ = butter[i] * resScale;                // the resonant stage
            } else {
                stageQ = butter[i];
            }
            stage[i].set(hz, stageQ, sr);
        }
    }

    inline float process(float x) {
        for (int i = 0; i < count; ++i) x = stage[i].process(x, shape);
        return x;
    }

    void reset() { for (auto& s : stage) s.reset(); }

private:
    SVF stage[kMaxStages];
    SVF::Type shape = SVF::LowPass;
    int count = 1;
};

// Selectable filter shapes and slopes. Order must match the plugin's
// "Filter Type" choice parameter.
struct FilterMode { SVF::Type shape; int stages; };
constexpr FilterMode kFilterModes[] = {
    { SVF::LowPass,  1 },   // Low Pass 12
    { SVF::LowPass,  2 },   // Low Pass 24
    { SVF::LowPass,  4 },   // Low Pass 48
    { SVF::HighPass, 1 },   // High Pass 12
    { SVF::HighPass, 2 },   // High Pass 24
    { SVF::HighPass, 4 },   // High Pass 48
    { SVF::BandPass, 1 },   // Band Pass 12
    { SVF::BandPass, 2 },   // Band Pass 24
    { SVF::Notch,    1 },   // Notch
    { SVF::Notch,    2 },   // Deep Notch
    { SVF::AllPass,  1 },   // All Pass
};
constexpr int kNumFilterModes = (int) (sizeof(kFilterModes) / sizeof(kFilterModes[0]));

class DelayLine {
public:
    void prepare(int maxSamples) {
        buf.assign(static_cast<size_t>(std::max(4, maxSamples)), 0.0f);
        write = 0;
    }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); write = 0; }

    inline void push(float x) {
        buf[static_cast<size_t>(write)] = x;
        if (++write >= static_cast<int>(buf.size())) write = 0;
    }

    // Fractional read, `d` samples back from the write head.
    inline float read(float d) const {
        const int n = static_cast<int>(buf.size());
        // std::clamp passes NaN straight through -- both of its comparisons
        // are false -- and (int)NaN is undefined, which indexes the buffer
        // out of bounds. Reject it before it can become an index.
        if (!std::isfinite(d)) d = 1.0f;
        d = std::clamp(d, 1.0f, static_cast<float>(n - 2));
        float rp = static_cast<float>(write) - d;
        while (rp < 0.0f) rp += static_cast<float>(n);
        int i0 = static_cast<int>(rp);
        if (i0 < 0 || i0 >= n) i0 = 0;   // unreachable, but never index blind
        const float f = rp - static_cast<float>(i0);
        const int i1 = (i0 + 1) % n;
        return buf[static_cast<size_t>(i0)] + f * (buf[static_cast<size_t>(i1)] - buf[static_cast<size_t>(i0)]);
    }
private:
    std::vector<float> buf;
    int write = 0;
};

// A delay that is set by the *input*: each incoming sample is scheduled to
// come out `d` samples later, and the delay can differ from one input sample
// to the next.
//
// An ordinary read-tap delay decides the delay at the output, so changing
// it moves the read head -- content gets skipped or played twice, which is
// exactly how a kick turned into a flam when swing switched the delay
// mid-tail. Scheduling at the input means every sample is played exactly
// once (split across two slots only while a crossfade is running).
class ScheduledDelay {
public:
    void prepare(int maxDelaySamples) {
        buf.assign(static_cast<size_t>(std::max(4, maxDelaySamples + 4)), 0.0f);
        pos = 0;
    }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); pos = 0; }

    float maxDelay() const { return static_cast<float>(buf.size()) - 3.0f; }

    // Add `x * weight` so it emerges `d` samples from now (0 = this sample).
    inline void schedule(float x, float d, float weight) {
        if (!std::isfinite(d)) d = 0.0f;
        d = std::clamp(d, 0.0f, maxDelay());
        const int i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        const int n = static_cast<int>(buf.size());
        const int i0 = (pos + i) % n;
        const int i1 = (i0 + 1) % n;
        buf[static_cast<size_t>(i0)] += x * weight * (1.0f - f);
        buf[static_cast<size_t>(i1)] += x * weight * f;
    }

    // Take the sample due now and advance.
    inline float pop() {
        float& slot = buf[static_cast<size_t>(pos)];
        const float y = slot;
        slot = 0.0f;
        if (++pos >= static_cast<int>(buf.size())) pos = 0;
        return y;
    }
private:
    std::vector<float> buf;
    int pos = 0;
};

// Schroeder allpass, used to smear the input before it reaches the network.
// Without a diffusion stage a small FDN fed with a kick produces audible
// discrete echoes -- the classic metallic flutter.
class Allpass {
public:
    void prepare(int maxSamples) { line.prepare(maxSamples); }
    void reset() { line.reset(); }
    void set(float delaySamples, float coeff) { d = delaySamples; g = coeff; }

    inline float process(float x) {
        const float delayed = line.read(d);
        const float v = x + g * delayed;
        line.push(v);
        return delayed - g * v;
    }
private:
    DelayLine line;
    float d = 1.0f, g = 0.5f;
};

// "Magic circle" quadrature oscillator: two multiplies per sample instead of
// a std::sin call. With eight lines per channel the trig cost was real.
struct QuadOsc {
    void setRate(float hz, float sr) { eps = 2.0f * kPi * hz / std::max(1.0f, sr); }
    void setPhase(float turns) { s = std::sin(2.0f * kPi * turns); c = std::cos(2.0f * kPi * turns); }
    inline float next() { s += eps * c; c -= eps * s; return s; }
    float s = 0.0f, c = 1.0f, eps = 0.0f;
};

class EnvelopeFollower {
public:
    void setTimes(float atkMs, float relMs, float sr) {
        atk = std::exp(-1.0f / (0.001f * std::max(0.05f, atkMs) * sr));
        rel = std::exp(-1.0f / (0.001f * std::max(1.0f, relMs) * sr));
    }
    inline float process(float x) {
        const float r = std::fabs(x);
        const float c = (r > env) ? atk : rel;
        env = r + c * (env - r);
        // The ducker saturates once the envelope reaches the threshold, so
        // anything above 0 dBFS is indistinguishable in gain terms -- but an
        // uncapped envelope takes seconds to decay back from a stray loud
        // sample, holding the output muted the whole time.
        env = std::min(env, kEnvelopeCeiling);
        return env;
    }
    void reset() { env = 0.0f; }
private:
    float atk = 0.0f, rel = 0.0f, env = 0.0f;
};

inline float softClip(float x) { return std::tanh(x); }

// Reject rubbish arriving from upstream. Non-finite values poison a feedback
// network permanently; absurd finite values blast through the dry path and
// pin the ducker. Real audio never exceeds +18 dBFS.
inline float sanitize(float x) {
    if (!std::isfinite(x)) return 0.0f;
    return std::clamp(x, -kMaxSample, kMaxSample);
}

// Continuous parameters that glide rather than step. Frequencies and other
// ratio-like values glide in the log domain, so a sweep sounds even.
struct SmoothedField { float Params::* field; bool logScale; };
inline constexpr SmoothedField kSmoothedFields[] = {
    { &Params::drive,        false }, { &Params::tailDrive,   false },
    { &Params::size,         false }, { &Params::decaySec,    true  },
    { &Params::dampHz,       true  }, { &Params::lowCutHz,    true  },
    { &Params::toneHz,       true  }, { &Params::diffusion,   false },
    { &Params::modDepth,     false }, { &Params::modRate,     true  },
    { &Params::duckThreshDb, false },
    { &Params::filterHz,     true  }, { &Params::filterQ,     true  },
    { &Params::enhance,      false }, { &Params::eqLowDb,     false },
    { &Params::eqMidDb,      false }, { &Params::eqMidHz,     true  },
    { &Params::eqHighDb,     false }, { &Params::monoBelowHz, true  },
};
// Output, Width and Duck are gains applied straight to the signal, where even
// a 32-sample staircase is audible, so they glide per sample instead (see
// RumbleEngine::processSample).

// ------------------------------------------------------------------- engine

class RumbleEngine {
public:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;

    void prepare(double sampleRate, int /*maxBlock*/) {
        sr = static_cast<float>(sampleRate);

        // Long enough for a tempo-synced whole note at slow tempos (1/1 at
        // 60 BPM is 4 s) with full swing.
        maxPredelaySamples = kMaxPredelayMs * 0.001f * sr;
        for (auto& p : predelay) p.prepare(static_cast<int>(maxPredelaySamples) + 4);
        maxEchoSamples = kMaxEchoMs * 0.001f * sr;
        for (auto& e : echo) e.prepare(static_cast<int>(maxEchoSamples) + 8);
        echoGlide = 1.0f - std::exp(-1.0f / (kEchoGlideMs * 0.001f * sr));
        predelayFadeStep = 1.0f / std::max(1.0f, kPredelayFadeMs * 0.001f * sr);
        gainGlide = 1.0f - std::exp(-1.0f / (kSmoothMs * 0.001f * sr));

        // Eight lines, lengths chosen with no simple integer ratios between
        // them so the modes spread out instead of stacking into a ringing
        // pitch.
        const float baseMs[kLines] = { 17.31f, 21.67f, 26.13f, 31.39f,
                                       36.73f, 43.07f, 49.31f, 57.73f };
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLines; ++i) {
                // Tiny per-channel offset gives a naturally decorrelated stereo tail.
                lineBaseMs[ch][i] = baseMs[i] * (ch == 0 ? 1.0f : 1.0193f);
                lines[ch][i].prepare(static_cast<int>(lineBaseMs[ch][i] * 0.001f * sr * 4.0f) + 64);
            }

        // Short, mutually irrational diffuser delays (Dattorro-style).
        const float diffMs[kDiffusers] = { 4.77f, 3.59f, 12.73f, 9.31f };
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kDiffusers; ++i) {
                diffuserMs[ch][i] = diffMs[i] * (ch == 0 ? 1.0f : 1.0271f);
                diffusers[ch][i].prepare(static_cast<int>(diffuserMs[ch][i] * 0.001f * sr) + 64);
            }

        reset();
        snapToTarget = true;
        smoothFrom(target);
    }

    void reset() {
        for (auto& p : predelay) p.reset();
        predelayCurrent = -1.0f;           // re-initialised on the next sample
        predelayFrom = 0.0f;
        predelayFade = 1.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLines; ++i) {
                lines[ch][i].reset();
                damp[ch][i].reset();
                lowCut[ch][i].reset();
                lfo[ch][i].setPhase(static_cast<float>(i) * 0.125f + static_cast<float>(ch) * 0.37f);
            }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kDiffusers; ++i) diffusers[ch][i].reset();
        for (int ch = 0; ch < 2; ++ch) {
            inHP[ch].reset(); inLP[ch].reset(); toneLP[ch].reset();
            svf[ch].reset(); enhLP[ch].reset(); enhHP[ch].reset();
            eqLow[ch].reset(); eqMid[ch].reset(); eqHigh[ch].reset();
            monoHP[ch].reset();
        }
        duckEnv.reset();
        for (int ch = 0; ch < 2; ++ch) { echo[ch].reset(); echoLP[ch].reset(); echoHP[ch].reset(); }
        echoSamplesNow = -1.0f;    // snaps to the target on the next sample
        echoRunning = false;
    }

    // New target settings. Continuous parameters glide towards them over
    // ~kSmoothMs; discrete ones (filter type, sync) switch immediately.
    void setParams(const Params& p) { target = scrub(p); paramsChanged = true; }
    const Params& getParams() const { return target; }

    // Lowest duck gain (0..1, 1 = no ducking) reached during the last
    // process() call. For metering.
    float lastMinDuckGain() const { return minDuckGain; }

    // In-place stereo processing. `sidechain` is optional mono trigger
    // material; when null the plugin's own input drives the ducker.
    // `timeline` is needed for tempo sync and swing; without it sync runs at
    // 120 BPM and swing is off. If `duckGainOut` is given, it receives the
    // duck gain (0..1) for every sample, for metering and display.
    void process(float* left, float* right, int numSamples,
                 const float* sidechain = nullptr, const Timeline* timeline = nullptr,
                 float* duckGainOut = nullptr) {
        minDuckGain = 1.0f;

        double bpm = 120.0;
        bool ppqValid = false;
        double ppq = 0.0;
        if (timeline != nullptr) {
            if (std::isfinite(timeline->bpm) && timeline->bpm > 0.0)
                bpm = std::clamp(timeline->bpm, 20.0, 999.0);
            if (timeline->valid && std::isfinite(timeline->ppq)) {
                ppqValid = true;
                ppq = timeline->ppq;
            }
        }
        const double ppqPerSample = bpm / 60.0 / static_cast<double>(sr);

        // Echo time for this block: tempo only changes between blocks.
        {
            const float ms = target.sync
                ? static_cast<float>(static_cast<double>(target.delayBeats) * 60000.0 / bpm)
                : target.delayMs;
            echoTargetSamples = std::clamp(ms * 0.001f * sr, 1.0f, maxEchoSamples);
        }

        for (int start = 0; start < numSamples; start += kSubBlock) {
            const int n = std::min(kSubBlock, numSamples - start);
            advanceSmoothing(n);
            for (int i = start; i < start + n; ++i) {
                const float pd = predelayTarget(ppq + static_cast<double>(i) * ppqPerSample,
                                                ppqValid, bpm);
                const float gain = processSample(left[i], right[i], sidechain ? sidechain + i : nullptr, pd);
                if (duckGainOut != nullptr) duckGainOut[i] = gain;
            }
        }
    }

private:
    static Params scrub(const Params& in) {
        // A NaN here would survive std::clamp and end up as a delay-line
        // index, so every field is forced back to a usable value before
        // anything downstream sees it.
        Params p = in;
        const Params def {};
        auto fix = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        p.predelayMs  = fix(p.predelayMs,  def.predelayMs);
        p.divBeats    = fix(p.divBeats,    def.divBeats);
        p.swing       = fix(p.swing,       def.swing);
        p.duckAtkMs   = fix(p.duckAtkMs,   def.duckAtkMs);
        p.delayMix      = std::clamp(fix(p.delayMix,      def.delayMix),      0.0f, 1.0f);
        p.delayMs       = std::clamp(fix(p.delayMs,       def.delayMs),       1.0f, kMaxEchoMs);
        p.delayBeats    = std::clamp(fix(p.delayBeats,    def.delayBeats),    1.0f / 64.0f, 16.0f);
        p.delayFeedback = std::clamp(fix(p.delayFeedback, def.delayFeedback), 0.0f, kMaxEchoFeedback);
        p.duckRelMs   = fix(p.duckRelMs,   def.duckRelMs);
        for (const auto& s : kSmoothedFields) {
            float& v = p.*(s.field);
            v = fix(v, def.*(s.field));
            // Log-domain smoothing needs strictly positive values.
            if (s.logScale) v = std::max(v, 1.0e-3f);
        }
        p.divBeats = std::clamp(p.divBeats, 1.0f / 64.0f, 16.0f);
        p.swing    = std::clamp(p.swing, 0.5f, 0.75f);
        p.filterType = std::clamp(p.filterType, 0, kNumFilterModes - 1);
        return p;
    }

    void smoothFrom(const Params& p) {
        current = p;
        outGainNow = p.outGain;
        widthNow = p.width;
        duckAmountNow = p.duckAmount;
        echoMixNow = p.delayMix;
        echoFeedbackNow = p.delayFeedback;
        applyCoefficients(current);
        paramsChanged = false;
    }

    // Move the smoothed parameters one sub-block closer to the target and
    // recompute coefficients if anything changed.
    void advanceSmoothing(int n) {
        if (snapToTarget) {
            snapToTarget = false;
            smoothFrom(target);
            return;
        }

        bool changed = paramsChanged;
        paramsChanged = false;

        // Discrete and timing-only fields take the target directly.
        current.predelayMs = target.predelayMs;
        current.sync       = target.sync;
        current.divBeats   = target.divBeats;
        current.swing      = target.swing;
        current.duckAtkMs  = target.duckAtkMs;
        current.duckRelMs  = target.duckRelMs;
        current.filterType = target.filterType;
        current.outGain    = target.outGain;     // glided per sample instead
        current.width      = target.width;
        current.duckAmount = target.duckAmount;
        current.delayMix      = target.delayMix;       // glided per sample
        current.delayFeedback = target.delayFeedback;
        current.delayMs       = target.delayMs;        // glided as echo time
        current.delayBeats    = target.delayBeats;
        current.delayPingPong = target.delayPingPong;

        const float a = 1.0f - std::exp(-static_cast<float>(n) / (kSmoothMs * 0.001f * sr));
        for (const auto& s : kSmoothedFields) {
            float& c = current.*(s.field);
            const float t = target.*(s.field);
            if (!(std::fabs(t - c) > 0.0f)) continue;   // settled
            changed = true;
            if (s.logScale) c = std::exp(std::log(c) + a * (std::log(t) - std::log(c)));
            else            c += a * (t - c);
            // Close enough: land exactly so the work stops.
            if (std::fabs(t - c) <= 1.0e-4f * std::max(1.0f, std::fabs(t))) c = t;
        }

        if (changed) applyCoefficients(current);
    }

    // Pre-delay, in samples, for an input sample at timeline position `ppq`.
    float predelayTarget(double ppq, bool ppqValid, double bpm) const {
        // Whole samples only. Pre-delay needs no sub-sample precision, and a
        // fractional delay would run every kick through linear interpolation,
        // whose high-frequency loss changes with the fractional part -- so
        // two delay times would colour the tail differently.
        if (!target.sync)
            return std::round(std::clamp(target.predelayMs * 0.001f * sr, 0.0f, maxPredelaySamples));

        const double div = target.divBeats;
        double beats = div;

        // Swing delays every second slot of the grid. A tail launched from an
        // even slot lands in the following odd slot -- the late one -- so it
        // is the input's slot that decides. Looking ahead means the crossfade
        // finishes before the slot boundary, so a kick starting on (or just
        // before) the boundary gets a single, clean delay time.
        if (ppqValid && target.swing > 0.5f) {
            const double aheadBeats = (kSwingLookaheadMs * 0.001) * bpm / 60.0;
            const double slot = std::floor((ppq + aheadBeats) / div + 1.0e-9);
            const bool evenSlot = std::fmod(std::fabs(slot), 2.0) < 0.5;
            if (evenSlot)
                beats += (2.0 * static_cast<double>(target.swing) - 1.0) * div;
        }

        const double samples = beats * 60.0 / bpm * static_cast<double>(sr);
        return static_cast<float>(std::round(std::clamp(samples, 0.0, static_cast<double>(maxPredelaySamples))));
    }

    // Returns the duck gain applied to this sample.
    inline float processSample(float& left, float& right, const float* sidechain, float predelayNow) {
        const Params& p = current;
        outGainNow    += gainGlide * (target.outGain    - outGainNow);
        echoMixNow      += gainGlide * (target.delayMix      - echoMixNow);
        echoFeedbackNow += gainGlide * (target.delayFeedback - echoFeedbackNow);
        widthNow      += gainGlide * (target.width      - widthNow);
        duckAmountNow += gainGlide * (target.duckAmount - duckAmountNow);

        // A feedback network recirculates its state forever, so a single
        // Inf/NaN from upstream would poison the reverb permanently. Reject
        // bad input at the boundary instead.
        const float dryL = sanitize(left);
        const float dryR = sanitize(right);

        // --- duck envelope follows the kick (internal or external trigger).
        // Full depth is reached when the trigger hits the threshold.
        const float trig = sidechain ? sanitize(*sidechain) : 0.5f * (dryL + dryR);
        const float duckGain =
            1.0f - duckAmountNow * std::min(1.0f, duckEnv.process(trig) * duckThreshInv);
        minDuckGain = std::min(minDuckGain, duckGain);

        // --- pre-delay bookkeeping. A change of delay time starts a short
        // crossfade instead of a jump; a further change during the fade waits
        // until it has finished, so the weights never jump.
        if (predelayCurrent < 0.0f) {
            predelayCurrent = predelayNow;
            predelayFade = 1.0f;
        } else if (predelayFade >= 1.0f && std::fabs(predelayNow - predelayCurrent) > 0.01f) {
            predelayFrom = predelayCurrent;
            predelayCurrent = predelayNow;
            predelayFade = 0.0f;
        }
        const float fade = predelayFade;
        if (predelayFade < 1.0f) predelayFade = std::min(1.0f, predelayFade + predelayFadeStep);

        float wet[2];
        for (int ch = 0; ch < 2; ++ch) {
            const float dry = (ch == 0) ? dryL : dryR;

            // Pre-shaping: only feed the reverb what should rumble.
            float x = inLP[ch].process(inHP[ch].process(dry));
            x = softClip(x * p.drive) * inputTrim;

            if (fade < 1.0f) {
                predelay[ch].schedule(x, predelayFrom, 1.0f - fade);
                predelay[ch].schedule(x, predelayCurrent, fade);
            } else {
                predelay[ch].schedule(x, predelayCurrent, 1.0f);
            }
            float fed = predelay[ch].pop();

            // --- diffusion: smear the input so the network is excited by
            // a dense cloud rather than a single spike.
            for (int i = 0; i < kDiffusers; ++i)
                fed = diffusers[ch][i].process(fed);

            // --- read the delay lines (modulated taps)
            float v[kLines];
            for (int i = 0; i < kLines; ++i) {
                const float mod = lfo[ch][i].next() * modSamples[i];
                v[i] = lines[ch][i].read(lineSamples[ch][i] + mod);
            }

            // --- 8x8 Hadamard via three butterfly stages. Orthogonal, so
            // it redistributes energy without adding or losing any.
            float m[kLines];
            for (int i = 0; i < kLines; ++i) m[i] = v[i];
            for (int step = 1; step < kLines; step <<= 1)
                for (int i = 0; i < kLines; i += step << 1)
                    for (int j = i; j < i + step; ++j) {
                        const float a = m[j], b = m[j + step];
                        m[j] = a + b;
                        m[j + step] = a - b;
                    }
            for (int i = 0; i < kLines; ++i) m[i] *= kHadamardScale;

            for (int i = 0; i < kLines; ++i) {
                float fb = m[i] * fbGain[ch][i];
                fb = damp[ch][i].process(fb);       // high damping -> dark tail
                fb = lowCut[ch][i].process(fb);     // lows decay faster -> no mud
                if (!std::isfinite(fb)) fb = 0.0f;  // never recirculate a bad value
                // Alternating injection polarity decorrelates the lines.
                lines[ch][i].push((i & 1 ? -fed : fed) + fb);
            }

            float out = 0.0f;
            for (int i = 0; i < kLines; ++i) out += v[i];
            out *= kHadamardScale;
            out = toneLP[ch].process(out);

            // Resonant filter before the saturator: sweeps stay clean, and
            // the drive then thickens whatever the filter left behind.
            out = svf[ch].process(out);

            out = softClip(out * p.tailDrive) * tailTrim;

            // Enhancer: distort only the low band, keep just the harmonics
            // it generates, and add them back. Adding the whole distorted
            // band would simply double the sub instead of reinforcing it.
            if (p.enhance > 0.0f) {
                const float low  = enhLP[ch].process(out);
                // 12 dB/oct high-pass: a one-pole leaks the fundamental
                // straight back in, which just doubles the sub.
                const float harm = enhHP[ch].process(softClip(low * 6.0f), SVF::HighPass);
                out += harm * p.enhance * 0.7f;
            }

            // EQ last, so it voices whatever the drive and enhancer made.
            out = eqLow[ch].process(out);
            out = eqMid[ch].process(out);
            out = eqHigh[ch].process(out);

            wet[ch] = out;
        }

        processEcho(wet);
        wet[0] *= duckGain;
        wet[1] *= duckGain;

        // --- stereo width, with the sub-band forced to mono. The side
        // channel goes through a 24 dB/oct (Linkwitz-Riley) high pass: a
        // one-pole only took the side down ~10 dB an octave below the
        // corner, which left plenty of stereo sub.
        const float mid  = 0.5f * (wet[0] + wet[1]);
        float side = 0.5f * (wet[0] - wet[1]) * widthNow;
        side = monoHP[0].process(side, SVF::HighPass);
        side = monoHP[1].process(side, SVF::HighPass);

        // Final guard: whatever happens upstream, never hand the host a
        // value big enough to hurt a speaker or the next plugin.
        left  = sanitize((mid + side) * outGainNow);
        right = sanitize((mid - side) * outGainNow);
        return duckGain;
    }

    // Feedback echo on the wet tail. Repeats pass through the Tone low pass
    // and Low Cut high pass on every trip, so each one is darker than the
    // last and the sub never builds up; a soft clipper keeps the loop tame.
    inline void processEcho(float (&wet)[2]) {
        // Skip the stage while it is off, so Delay at 0% costs nothing and
        // changes nothing. Once off, the lines are cleared before the next
        // use so stale repeats from long ago never reappear.
        const bool wanted = target.delayMix > 0.0f || echoMixNow > 1.0e-5f;
        if (!wanted) {
            if (echoRunning) {
                echoRunning = false;
                echoNeedsClear = true;
            }
            echoMixNow = 0.0f;
            return;
        }
        if (!echoRunning) {
            if (echoNeedsClear) {
                for (int ch = 0; ch < 2; ++ch) { echo[ch].reset(); echoLP[ch].reset(); echoHP[ch].reset(); }
                echoNeedsClear = false;
            }
            echoRunning = true;
            echoSamplesNow = echoTargetSamples;   // start at the right time, no sweep
        }

        if (echoSamplesNow < 0.0f) echoSamplesNow = echoTargetSamples;
        echoSamplesNow += echoGlide * (echoTargetSamples - echoSamplesNow);

        const float tapL = echo[0].read(echoSamplesNow);
        const float tapR = echo[1].read(echoSamplesNow);

        auto loop = [this](int ch, float x) {
            x = echoLP[ch].process(x);
            x = echoHP[ch].process(x);
            return softClip(x);
        };
        const float fb = echoFeedbackNow;

        if (current.delayPingPong) {
            // Mono into the left line; each line feeds the other, so the
            // repeats alternate sides.
            const float in = 0.5f * (wet[0] + wet[1]);
            echo[0].push(sanitize(in + fb * loop(1, tapR)));
            echo[1].push(sanitize(     fb * loop(0, tapL)));
        } else {
            echo[0].push(sanitize(wet[0] + fb * loop(0, tapL)));
            echo[1].push(sanitize(wet[1] + fb * loop(1, tapR)));
        }

        wet[0] += echoMixNow * tapL;
        wet[1] += echoMixNow * tapR;
    }

    void applyCoefficients(const Params& p) {
        // Magnitude of the in-loop one-pole high pass at a mid-band reference
        // frequency, used to undo its passband loss (see fbGain below).
        {
            const float a = 1.0f - std::exp(-2.0f * kPi * std::clamp(p.lowCutHz, 10.0f, sr * 0.49f) / sr);
            const float b = 1.0f - a;
            const float w = 2.0f * kPi * kDecayRefHz / sr;
            const float cw = std::cos(w), sw = std::sin(w);
            const float denRe = 1.0f - b * cw, denIm = b * sw;
            const float den2 = denRe * denRe + denIm * denIm;
            const float hRe = 1.0f - a * denRe / den2;
            const float hIm = a * denIm / den2;
            const float mag = std::sqrt(hRe * hRe + hIm * hIm);
            lowCutMakeup = (mag > 1.0e-4f) ? std::clamp(1.0f / mag, 1.0f, kMaxLowCutMakeup) : 1.0f;
        }

        const float sizeMul = 0.35f + 1.65f * std::clamp(p.size, 0.0f, 1.0f);
        const float rt60 = std::clamp(p.decaySec, 0.05f, 60.0f);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLines; ++i) {
                const float ms = lineBaseMs[ch][i] * sizeMul;
                lineSamples[ch][i] = ms * 0.001f * sr;
                // RT60 -> per-loop gain: g = 10^(-3 * delay / RT60)
                fbGain[ch][i] = std::pow(10.0f, -3.0f * (ms * 0.001f) / rt60);
                // The in-loop low cut also nibbles at the mid band, which
                // would make the tail shorter than the Decay knob promises.
                // Give the gain that loss back, but only a little: past a
                // point the user is deliberately gutting the lows and should
                // get a shorter tail.
                fbGain[ch][i] *= lowCutMakeup;
                fbGain[ch][i] = std::min(fbGain[ch][i], 0.9995f);
                damp[ch][i].setCutoff(p.dampHz, sr);
                lowCut[ch][i].setCutoff(p.lowCutHz, sr);
            }

        for (int i = 0; i < kLines; ++i) {
            // Eight uncorrelated rates. The 0.043 Hz spacing is what makes the
            // bank beat against itself, so the tail keeps drifting into new
            // configurations instead of repeating: adjacent pairs beat every
            // 1/0.043 = 23 s, the slowest LFO cycles every 9 s, and the whole
            // bank never quite repeats. Mod Rate scales all of it.
            const float rateScale = std::clamp(p.modRate, 0.05f, 8.0f);
            const float rateHz = (0.11f + 0.043f * static_cast<float>(i)) * rateScale;
            for (int ch = 0; ch < 2; ++ch) lfo[ch][i].setRate(rateHz, sr);
            modSamples[i] = std::clamp(p.modDepth, 0.0f, 1.0f) * 0.0025f * sr;
        }

        // Diffusion coefficient: enough to smear, short of self-oscillation.
        const float dg = 0.30f + 0.40f * std::clamp(p.diffusion, 0.0f, 1.0f);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kDiffusers; ++i)
                diffusers[ch][i].set(diffuserMs[ch][i] * 0.001f * sr, dg);

        const FilterMode mode = kFilterModes[std::clamp(p.filterType, 0, kNumFilterModes - 1)];
        const float monoHz = std::clamp(p.monoBelowHz, 20.0f, 1000.0f);
        for (int ch = 0; ch < 2; ++ch) {
            svf[ch].configure(mode.shape, mode.stages, p.filterHz, p.filterQ, sr);
            eqLow[ch].set(Biquad::LowShelf,  kEqLowHz,  p.eqLowDb,  0.8f, sr);
            eqMid[ch].set(Biquad::Peak,      p.eqMidHz, p.eqMidDb,  1.1f, sr);
            eqHigh[ch].set(Biquad::HighShelf, kEqHighHz, p.eqHighDb, 0.8f, sr);
            enhLP[ch].setCutoff(kEnhanceBandHz, sr);
            enhHP[ch].set(kEnhanceBandHz, 0.7f, sr);
            inHP[ch].setCutoff(std::max(20.0f, p.lowCutHz), sr);
            inLP[ch].setCutoff(std::clamp(p.dampHz * 1.5f, 80.0f, sr * 0.45f), sr);
            toneLP[ch].setCutoff(std::clamp(p.toneHz, 40.0f, sr * 0.45f), sr);
            // Two Butterworth stages = Linkwitz-Riley 24 dB/oct.
            monoHP[ch].set(monoHz, 0.70710678f, sr);
        }

        // Compensate by the saturator's small-signal slope (tanh'(0) = drive).
        // Anything else boosts quiet signals: a 1/sqrt(drive) trim gives a
        // decaying tail *rising* gain as it fades, which sounds like the tail
        // swelling up out of silence.
        for (int ch = 0; ch < 2; ++ch) {
            echoLP[ch].setCutoff(std::clamp(p.toneHz, 40.0f, sr * 0.45f), sr);
            echoHP[ch].setCutoff(std::max(20.0f, p.lowCutHz), sr);
        }

        inputTrim = 1.0f / std::max(1.0f, p.drive);
        tailTrim  = 1.0f / std::max(1.0f, p.tailDrive);

        const float threshDb = std::clamp(p.duckThreshDb, -60.0f, 0.0f);
        duckThreshInv = 1.0f / std::pow(10.0f, threshDb / 20.0f);

        duckEnv.setTimes(p.duckAtkMs, p.duckRelMs, sr);
    }

    Params target;                 // what the host asked for
    Params current;                // what is being played, gliding towards target
    bool paramsChanged = true;
    bool snapToTarget = true;
    float sr = 48000.0f;

    ScheduledDelay predelay[2];
    float maxPredelaySamples = 0.0f;
    float predelayCurrent = -1.0f; // delay being faded to (or held), samples
    float predelayFrom = 0.0f;     // delay being faded from
    float predelayFade = 1.0f;     // 0..1 progress of the current fade
    float predelayFadeStep = 0.01f;

    Allpass diffusers[2][kDiffusers];
    float diffuserMs[2][kDiffusers] {};
    DelayLine lines[2][kLines];
    OnePoleLP damp[2][kLines];
    OnePoleHP lowCut[2][kLines];
    float lowCutMakeup = 1.0f;
    OnePoleHP inHP[2];
    OnePoleLP inLP[2], toneLP[2], enhLP[2];
    FilterCascade svf[2];
    SVF enhHP[2];
    SVF monoHP[2];                 // two stages on the side channel
    Biquad eqLow[2], eqMid[2], eqHigh[2];
    EnvelopeFollower duckEnv;
    float duckThreshInv = 2.0f;
    float gainGlide = 0.001f;      // per-sample one-pole coefficient
    float outGainNow = 1.0f, widthNow = 1.0f, duckAmountNow = 0.85f;

    DelayLine echo[2];
    OnePoleLP echoLP[2];
    OnePoleHP echoHP[2];
    float maxEchoSamples = 0.0f;
    float echoTargetSamples = 1.0f;
    float echoSamplesNow = -1.0f;
    float echoGlide = 0.001f;
    float echoMixNow = 0.0f, echoFeedbackNow = 0.35f;
    bool echoRunning = false;
    bool echoNeedsClear = false;
    float minDuckGain = 1.0f;

    float lineBaseMs[2][kLines] {};
    float lineSamples[2][kLines] {};
    float fbGain[2][kLines] {};
    QuadOsc lfo[2][kLines];
    float modSamples[kLines] {};
    float inputTrim = 1.0f;
    float tailTrim = 1.0f;
};

} // namespace rumble
