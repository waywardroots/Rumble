// RumbleEngine.h — framework-agnostic DSP core for the Rumble reverb.
//
// Signal flow (per block):
//   in -> hp/lp shaping -> predelay -> FDN reverb (4 lines, Hadamard mix,
//   damped + low-cut feedback, chorused delay taps) -> tail drive ->
//   sidechain duck (envelope of the dry input) -> width / mono-below -> out
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
    float predelayMs = 8.0f;   // 0..kMaxPredelayMs (host may sync this to tempo)
    float size       = 0.35f;  // 0..1 (small rooms rumble tighter)
    float decaySec   = 3.5f;   // 0.2..30 RT60
    float dampHz     = 1200.f; // feedback high-cut: kills the "air", keeps weight
    float lowCutHz   = 30.f;   // feedback low-cut: stops mud / DC build-up
    float toneHz     = 400.f;  // post high-cut on the wet signal
    float diffusion  = 0.70f;  // 0..1 input smearing before the network
    float modDepth   = 0.25f;  // 0..1 delay modulation (smears the metallic ring)
    float modRate    = 1.0f;   // scales the LFO bank; 1.0 = movement every ~9-23 s
    float duckAmount = 0.85f;  // 0..1 how hard the input ducks the tail
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

    float width      = 1.0f;   // 0..2 stereo width of the tail
    float monoBelowHz= 140.f;  // sub stays centred
    float outGain    = 1.0f;
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

// Topology-preserving-transform state variable filter (Zavalishin). Unlike the
// one-poles above this one resonates, and it stays stable when the cutoff is
// swept quickly -- which is the whole point of having it.
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
        // The ducker saturates once the envelope passes 0.5, so anything
        // above the ceiling is indistinguishable in gain terms -- but an
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

// ------------------------------------------------------------------- engine

class RumbleEngine {
public:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;

    void prepare(double sampleRate, int /*maxBlock*/) {
        sr = static_cast<float>(sampleRate);

        // Long enough for a tempo-synced whole note at slow tempos (1/1 at
        // 60 BPM is 4 s).
        const int maxPredelay = static_cast<int>(kMaxPredelayMs * 0.001f * sr) + 4;
        for (auto& p : predelay) p.prepare(maxPredelay);

        // Eight lines, lengths chosen with no simple integer ratios between
        // them so the modes spread out instead of stacking into a ringing
        // pitch. Twice the line count doubles the echo density.
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
        update(params, true);
    }

    void reset() {
        for (auto& p : predelay) p.reset();
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
            inHP[ch].reset(); inLP[ch].reset(); toneLP[ch].reset(); monoHP[ch].reset(); monoLP[ch].reset();
            svf[ch].reset(); enhLP[ch].reset(); enhHP[ch].reset();
            eqLow[ch].reset(); eqMid[ch].reset(); eqHigh[ch].reset();
        }
        duckEnv.reset();
    }

    void setParams(const Params& p) { update(p, false); }
    const Params& getParams() const { return params; }

    // In-place stereo processing. `sidechain` is optional mono trigger material;
    // when null the plugin's own input drives the ducker.
    void process(float* left, float* right, int numSamples, const float* sidechain = nullptr) {
        for (int n = 0; n < numSamples; ++n) {
            // A feedback network recirculates its state forever, so a single
            // Inf/NaN from upstream would poison the reverb permanently -- and
            // a zero Mix would not hide it, because 0 * NaN is NaN. Reject bad
            // input at the boundary instead.
            const float dryL = sanitize(left[n]);
            const float dryR = sanitize(right[n]);

            // --- duck envelope follows the kick (internal or external trigger).
            const float trig = sidechain ? sanitize(sidechain[n]) : 0.5f * (dryL + dryR);
            const float duckGain =
                1.0f - params.duckAmount * std::min(1.0f, duckEnv.process(trig) * 2.0f);

            float wet[2];
            for (int ch = 0; ch < 2; ++ch) {
                const float dry = (ch == 0) ? dryL : dryR;

                // Pre-shaping: only feed the reverb what should rumble.
                float x = inLP[ch].process(inHP[ch].process(dry));
                x = softClip(x * params.drive) * inputTrim;

                predelay[ch].push(x);
                float fed = predelay[ch].read(predelaySamples);

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

                out = softClip(out * params.tailDrive) * tailTrim;

                // Enhancer: distort only the low band, keep just the harmonics
                // it generates, and add them back. Adding the whole distorted
                // band would simply double the sub instead of reinforcing it.
                if (params.enhance > 0.0f) {
                    const float low  = enhLP[ch].process(out);
                    // 12 dB/oct high-pass: a one-pole leaks the fundamental
                    // straight back in, which just doubles the sub.
                    const float harm = enhHP[ch].process(softClip(low * 6.0f), SVF::HighPass);
                    out += harm * params.enhance * 0.7f;
                }

                // EQ last, so it voices whatever the drive and enhancer made.
                out = eqLow[ch].process(out);
                out = eqMid[ch].process(out);
                out = eqHigh[ch].process(out);

                wet[ch] = out * duckGain;
            }

            // --- stereo width, with the sub-band forced to mono
            float mid  = 0.5f * (wet[0] + wet[1]);
            float side = 0.5f * (wet[0] - wet[1]) * params.width;
            side = monoHP[0].process(side);  // remove lows from the side channel
            float wl = mid + side;
            float wr = mid - side;

            // Final guard: whatever happens upstream, never hand the host a
            // value big enough to hurt a speaker or the next plugin.
            left[n]  = sanitize(wl * params.outGain);
            right[n] = sanitize(wr * params.outGain);
        }
    }

private:
    void update(const Params& in, bool force) {
        (void)force;

        // Scrub the incoming parameters. A NaN here would survive std::clamp
        // and end up as a delay-line index, so every field is forced back to
        // a usable value before anything downstream sees it.
        Params p = in;
        const Params def {};
        auto fix = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        p.drive       = fix(p.drive,       def.drive);
        p.tailDrive   = fix(p.tailDrive,   def.tailDrive);
        p.predelayMs  = fix(p.predelayMs,  def.predelayMs);
        p.size        = fix(p.size,        def.size);
        p.decaySec    = fix(p.decaySec,    def.decaySec);
        p.dampHz      = fix(p.dampHz,      def.dampHz);
        p.lowCutHz    = fix(p.lowCutHz,    def.lowCutHz);
        p.toneHz      = fix(p.toneHz,      def.toneHz);
        p.filterHz    = fix(p.filterHz,    def.filterHz);
        p.filterQ     = fix(p.filterQ,     def.filterQ);
        p.enhance     = fix(p.enhance,     def.enhance);
        p.eqLowDb     = fix(p.eqLowDb,     def.eqLowDb);
        p.eqMidDb     = fix(p.eqMidDb,     def.eqMidDb);
        p.eqMidHz     = fix(p.eqMidHz,     def.eqMidHz);
        p.eqHighDb    = fix(p.eqHighDb,    def.eqHighDb);
        p.diffusion   = fix(p.diffusion,   def.diffusion);
        p.modDepth    = fix(p.modDepth,    def.modDepth);
        p.modRate     = fix(p.modRate,     def.modRate);
        p.duckAmount  = fix(p.duckAmount,  def.duckAmount);
        p.duckAtkMs   = fix(p.duckAtkMs,   def.duckAtkMs);
        p.duckRelMs   = fix(p.duckRelMs,   def.duckRelMs);
        p.width       = fix(p.width,       def.width);
        p.monoBelowHz = fix(p.monoBelowHz, def.monoBelowHz);
        p.outGain     = fix(p.outGain,     def.outGain);

        params = p;

        predelaySamples = std::clamp(p.predelayMs, 0.0f, kMaxPredelayMs) * 0.001f * sr;
        if (predelaySamples < 1.0f) predelaySamples = 1.0f;

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
            monoHP[ch].setCutoff(std::clamp(p.monoBelowHz, 20.0f, 1000.0f), sr);
            monoLP[ch].setCutoff(std::clamp(p.monoBelowHz, 20.0f, 1000.0f), sr);
        }

        // Compensate by the saturator's small-signal slope (tanh'(0) = drive).
        // Anything else boosts quiet signals: a 1/sqrt(drive) trim gives a
        // decaying tail *rising* gain as it fades, which sounds like the tail
        // swelling up out of silence.
        inputTrim = 1.0f / std::max(1.0f, p.drive);
        tailTrim  = 1.0f / std::max(1.0f, p.tailDrive);

        duckEnv.setTimes(p.duckAtkMs, p.duckRelMs, sr);
    }

    Params params;
    float sr = 48000.0f;

    DelayLine predelay[2];
    Allpass diffusers[2][kDiffusers];
    float diffuserMs[2][kDiffusers] {};
    DelayLine lines[2][kLines];
    OnePoleLP damp[2][kLines];
    OnePoleHP lowCut[2][kLines];
    float lowCutMakeup = 1.0f;
    OnePoleHP inHP[2], monoHP[2];
    OnePoleLP enhLP[2];
    FilterCascade svf[2];
    SVF enhHP[2];
    Biquad eqLow[2], eqMid[2], eqHigh[2];
    SVF::Type filterTypeEnum = SVF::LowPass;
    OnePoleLP inLP[2], toneLP[2], monoLP[2];
    EnvelopeFollower duckEnv;

    float lineBaseMs[2][kLines] {};
    float lineSamples[2][kLines] {};
    float fbGain[2][kLines] {};
    QuadOsc lfo[2][kLines];
    float modSamples[kLines] {};
    float predelaySamples = 48.0f;
    float inputTrim = 1.0f;
    float tailTrim = 1.0f;
};

} // namespace rumble
