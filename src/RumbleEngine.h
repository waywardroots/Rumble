// RumbleEngine.h — framework-agnostic DSP core for the Rumble reverb.
//
// Signal flow (per block):
//   in -> hp/lp shaping -> predelay -> FDN reverb (4 lines, Hadamard mix,
//   damped + low-cut feedback, chorused delay taps) -> tail drive ->
//   sidechain duck (envelope of the dry input) -> width / mono-below -> mix
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

// Upper bound on pre-delay, sized for a synced 1/1 at 60 BPM plus headroom.
constexpr float kMaxPredelayMs = 4500.0f;

struct Params {
    float mix        = 1.0f;   // 0..1 dry/wet
    float drive      = 2.0f;   // 1..10 pre-reverb saturation
    float tailDrive  = 2.0f;   // 1..20 post-reverb saturation: harmonics that
                               // let the rumble read on small speakers
    float predelayMs = 8.0f;   // 0..kMaxPredelayMs (host may sync this to tempo)
    float size       = 0.35f;  // 0..1 (small rooms rumble tighter)
    float decaySec   = 3.5f;   // 0.2..30 RT60
    float dampHz     = 1200.f; // feedback high-cut: kills the "air", keeps weight
    float lowCutHz   = 30.f;   // feedback low-cut: stops mud / DC build-up
    float toneHz     = 400.f;  // post high-cut on the wet signal
    float modDepth   = 0.25f;  // 0..1 delay modulation (smears the metallic ring)
    float duckAmount = 0.85f;  // 0..1 how hard the input ducks the tail
    float duckAtkMs  = 2.0f;
    float duckRelMs  = 220.f;
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
        d = std::clamp(d, 1.0f, static_cast<float>(n - 2));
        float rp = static_cast<float>(write) - d;
        while (rp < 0.0f) rp += static_cast<float>(n);
        const int i0 = static_cast<int>(rp);
        const float f = rp - static_cast<float>(i0);
        const int i1 = (i0 + 1) % n;
        return buf[static_cast<size_t>(i0)] + f * (buf[static_cast<size_t>(i1)] - buf[static_cast<size_t>(i0)]);
    }
private:
    std::vector<float> buf;
    int write = 0;
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
        return env;
    }
    void reset() { env = 0.0f; }
private:
    float atk = 0.0f, rel = 0.0f, env = 0.0f;
};

inline float softClip(float x) { return std::tanh(x); }

// ------------------------------------------------------------------- engine

class RumbleEngine {
public:
    static constexpr int kLines = 4;

    void prepare(double sampleRate, int /*maxBlock*/) {
        sr = static_cast<float>(sampleRate);

        // Long enough for a tempo-synced whole note at slow tempos (1/1 at
        // 60 BPM is 4 s).
        const int maxPredelay = static_cast<int>(kMaxPredelayMs * 0.001f * sr) + 4;
        for (auto& p : predelay) p.prepare(maxPredelay);

        // Mutually prime-ish base lengths (ms) so modes don't stack up.
        const float baseMs[kLines] = { 23.13f, 31.71f, 41.27f, 53.89f };
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLines; ++i) {
                // Tiny per-channel offset gives a naturally decorrelated stereo tail.
                lineBaseMs[ch][i] = baseMs[i] * (ch == 0 ? 1.0f : 1.037f);
                lines[ch][i].prepare(static_cast<int>(lineBaseMs[ch][i] * 0.001f * sr * 4.0f) + 64);
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
                dcCut[ch][i].reset();
                fbState[ch][i] = 0.0f;
                lfoPhase[ch][i] = static_cast<float>(i) * 0.25f + static_cast<float>(ch) * 0.13f;
            }
        for (int ch = 0; ch < 2; ++ch) {
            inHP[ch].reset(); inLP[ch].reset(); toneLP[ch].reset(); monoHP[ch].reset(); monoLP[ch].reset();
        }
        duckEnv.reset();
    }

    void setParams(const Params& p) { update(p, false); }
    const Params& getParams() const { return params; }

    // In-place stereo processing. `sidechain` is optional mono trigger material;
    // when null the plugin's own input drives the ducker.
    void process(float* left, float* right, int numSamples, const float* sidechain = nullptr) {
        for (int n = 0; n < numSamples; ++n) {
            const float dryL = left[n];
            const float dryR = right[n];

            // --- duck envelope follows the kick (internal or external trigger).
            const float trig = sidechain ? sidechain[n] : 0.5f * (dryL + dryR);
            const float duckGain =
                1.0f - params.duckAmount * std::min(1.0f, duckEnv.process(trig) * 2.0f);

            float wet[2];
            for (int ch = 0; ch < 2; ++ch) {
                const float dry = (ch == 0) ? dryL : dryR;

                // Pre-shaping: only feed the reverb what should rumble.
                float x = inLP[ch].process(inHP[ch].process(dry));
                x = softClip(x * params.drive) * inputTrim;

                predelay[ch].push(x);
                const float fed = predelay[ch].read(predelaySamples);

                // --- read the four delay lines (modulated taps)
                float v[kLines];
                for (int i = 0; i < kLines; ++i) {
                    lfoPhase[ch][i] += lfoInc[i];
                    if (lfoPhase[ch][i] >= 1.0f) lfoPhase[ch][i] -= 1.0f;
                    const float mod = std::sin(2.0f * kPi * lfoPhase[ch][i]) * modSamples[i];
                    v[i] = lines[ch][i].read(lineSamples[ch][i] + mod);
                }

                // --- Hadamard mixing: cheap, lossless, maximal diffusion
                const float a0 = v[0] + v[1], a1 = v[0] - v[1];
                const float a2 = v[2] + v[3], a3 = v[2] - v[3];
                float m[kLines] = { (a0 + a2) * 0.5f, (a1 + a3) * 0.5f,
                                    (a0 - a2) * 0.5f, (a1 - a3) * 0.5f };

                for (int i = 0; i < kLines; ++i) {
                    float fb = m[i] * fbGain[ch][i];
                    fb = damp[ch][i].process(fb);       // high damping -> dark tail
                    fb = dcCut[ch][i].process(fb);      // low cut -> no mud build-up
                    fbState[ch][i] = fb;
                    lines[ch][i].push(fed + fb);
                }

                float out = 0.5f * (v[0] + v[1] + v[2] + v[3]);
                out = toneLP[ch].process(out);
                wet[ch] = softClip(out * params.tailDrive) * tailTrim * duckGain;
            }

            // --- stereo width, with the sub-band forced to mono
            float mid  = 0.5f * (wet[0] + wet[1]);
            float side = 0.5f * (wet[0] - wet[1]) * params.width;
            side = monoHP[0].process(side);  // remove lows from the side channel
            float wl = mid + side;
            float wr = mid - side;

            left[n]  = (dryL * (1.0f - params.mix) + wl * params.mix) * params.outGain;
            right[n] = (dryR * (1.0f - params.mix) + wr * params.mix) * params.outGain;
        }
    }

private:
    void update(const Params& p, bool force) {
        params = p;
        (void)force;

        predelaySamples = std::clamp(p.predelayMs, 0.0f, kMaxPredelayMs) * 0.001f * sr;
        if (predelaySamples < 1.0f) predelaySamples = 1.0f;

        const float sizeMul = 0.35f + 1.65f * std::clamp(p.size, 0.0f, 1.0f);
        const float rt60 = std::clamp(p.decaySec, 0.05f, 60.0f);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLines; ++i) {
                const float ms = lineBaseMs[ch][i] * sizeMul;
                lineSamples[ch][i] = ms * 0.001f * sr;
                // RT60 -> per-loop gain: g = 10^(-3 * delay / RT60)
                fbGain[ch][i] = std::pow(10.0f, -3.0f * (ms * 0.001f) / rt60);
                fbGain[ch][i] = std::min(fbGain[ch][i], 0.9995f);
                damp[ch][i].setCutoff(p.dampHz, sr);
                dcCut[ch][i].setCutoff(p.lowCutHz, sr);
            }

        for (int i = 0; i < kLines; ++i) {
            const float rateHz = 0.13f + 0.071f * static_cast<float>(i); // slow, uncorrelated
            lfoInc[i] = rateHz / sr;
            modSamples[i] = std::clamp(p.modDepth, 0.0f, 1.0f) * 0.0025f * sr;
        }

        for (int ch = 0; ch < 2; ++ch) {
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
    DelayLine lines[2][kLines];
    OnePoleLP damp[2][kLines];
    OnePoleHP dcCut[2][kLines];
    OnePoleHP inHP[2], monoHP[2];
    OnePoleLP inLP[2], toneLP[2], monoLP[2];
    EnvelopeFollower duckEnv;

    float lineBaseMs[2][kLines] {};
    float lineSamples[2][kLines] {};
    float fbGain[2][kLines] {};
    float fbState[2][kLines] {};
    float lfoPhase[2][kLines] {};
    float lfoInc[kLines] {};
    float modSamples[kLines] {};
    float predelaySamples = 48.0f;
    float inputTrim = 1.0f;
    float tailTrim = 1.0f;
};

} // namespace rumble
