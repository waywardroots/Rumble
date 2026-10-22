// Regression tests for the DSP core. Plain C++17, no JUCE, no framework:
//
//   g++ -O2 -std=c++17 tools/engine_tests.cpp -o rumble_tests && ./rumble_tests
//
// CMake builds this as `rumble_tests` and registers it with CTest, and CI runs
// it on every push. Each check prints PASS/FAIL with the measured value, so a
// failure says how far off it was, not just that it was.

#include "../src/RumbleEngine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kSr = 48000;
int failures = 0;

void check(bool ok, const std::string& what, double measured) {
    std::printf("%s  %-62s (%.4g)\n", ok ? "PASS" : "FAIL", what.c_str(), measured);
    if (!ok) ++failures;
}

struct Stereo { std::vector<float> l, r; };

// Run `in` through a fresh engine in blocks of `block`, optionally with a
// running timeline and a per-block parameter hook.
Stereo render(const rumble::Params& p, const Stereo& in, int block,
              double bpm = 0.0,
              const std::function<void(rumble::Params&, size_t)>& perBlock = nullptr) {
    rumble::RumbleEngine e;
    e.prepare(kSr, block);
    rumble::Params q = p;
    e.setParams(q);
    Stereo out = in;
    const double ppqPerSample = bpm / 60.0 / kSr;
    for (size_t i = 0; i < out.l.size(); i += (size_t) block) {
        const int n = (int) std::min<size_t>((size_t) block, out.l.size() - i);
        if (perBlock) { perBlock(q, i); e.setParams(q); }
        rumble::Timeline tl;
        tl.valid = bpm > 0.0;
        tl.bpm = bpm > 0.0 ? bpm : 120.0;
        tl.ppq = (double) i * ppqPerSample;
        e.process(out.l.data() + i, out.r.data() + i, n, nullptr, &tl);
    }
    return out;
}

Stereo silence(double seconds) {
    const size_t n = (size_t) (seconds * kSr);
    return { std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
}

// A neutral, linear-ish setting: no drive, no ducking, filters wide open, so
// measurements see the reverb itself.
rumble::Params neutral() {
    rumble::Params p;
    p.drive = 1.0f; p.tailDrive = 1.0f; p.duckAmount = 0.0f;
    p.toneHz = 5000.0f; p.dampHz = 8000.0f; p.lowCutHz = 20.0f;
    p.predelayMs = 0.0f; p.modDepth = 0.0f; p.width = 1.0f; p.monoBelowHz = 20.0f;
    return p;
}

double energy(const std::vector<float>& x, size_t a, size_t b) {
    double e = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) e += (double) x[i] * x[i];
    return e;
}

// RT60 from the Schroeder backward integral, fitted between -5 and -25 dB.
double measureRt60(const std::vector<float>& ir) {
    std::vector<double> edc(ir.size());
    double acc = 0.0;
    for (size_t i = ir.size(); i-- > 0;) { acc += (double) ir[i] * ir[i]; edc[i] = acc; }
    const double total = edc[0];
    size_t a = 0, b = 0;
    for (size_t i = 0; i < edc.size(); ++i) {
        const double db = 10.0 * std::log10(edc[i] / total + 1e-30);
        if (a == 0 && db <= -5.0) a = i;
        if (db <= -25.0) { b = i; break; }
    }
    if (a == 0 || b <= a) return 0.0;
    return 3.0 * (double) (b - a) / kSr;   // 20 dB span -> x3 for 60 dB
}

// ------------------------------------------------------------------ tests

void testDecayMatchesKnob() {
    for (float rt : { 1.0f, 4.0f, 10.0f }) {
        rumble::Params p = neutral();
        p.decaySec = rt;
        Stereo in = silence(rt * 1.6 + 0.5);
        in.l[0] = in.r[0] = 1.0f;
        const Stereo out = render(p, in, 256);
        // Decay is specified at 400 Hz, so measure that octave: high bands
        // die faster through Damping and low ones through Low Cut, by design.
        rumble::SVF band;
        band.set(400.0f, 1.41f, (float) kSr);
        std::vector<float> ir(out.l.size());
        for (size_t i = 0; i < ir.size(); ++i) ir[i] = band.process(out.l[i], rumble::SVF::BandPass);
        const double measured = measureRt60(ir);
        const double err = std::fabs(measured - rt) / rt;
        check(err < 0.12, "RT60 at 400 Hz within 12% of Decay = " + std::to_string((int) rt) + " s", measured);
    }
}

void testBadInputStaysFinite() {
    rumble::Params p;  // defaults, drive and ducking on
    Stereo in = silence(3.0);
    std::mt19937 g(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (size_t i = 0; i < in.l.size(); ++i) { in.l[i] = u(g); in.r[i] = u(g); }
    in.l[1000] = NAN; in.r[2000] = INFINITY; in.l[3000] = 1.0e30f;
    const Stereo out = render(p, in, 512);
    bool finite = true;
    float peak = 0.0f;
    for (size_t i = 0; i < out.l.size(); ++i) {
        finite &= std::isfinite(out.l[i]) && std::isfinite(out.r[i]);
        peak = std::max(peak, std::max(std::fabs(out.l[i]), std::fabs(out.r[i])));
    }
    check(finite, "NaN / Inf / huge input never reach the output", finite ? 1.0 : 0.0);
    check(peak <= rumble::kMaxSample, "output stays within the +18 dBFS clamp", peak);
}

// Four-on-the-floor impulses, synced 1/16 with swing. Every kick must come
// out exactly once, at the swung time, with the same energy as unswung.
void testSwingSchedulesEachKickOnce() {
    const double bpm = 135.0;
    const double quarter = 60.0 / bpm * kSr;
    const int beats = 8;

    auto run = [&](float swing) {
        rumble::Params p = neutral();
        p.decaySec = 0.2f;           // short, so each kick's response is separable
        p.diffusion = 0.0f;
        p.sync = true; p.divBeats = 0.25f; p.swing = swing;
        Stereo in = silence((beats + 1) * quarter / kSr);
        for (int b = 0; b < beats; ++b) in.l[(size_t) std::llround(b * quarter)] = in.r[(size_t) std::llround(b * quarter)] = 1.0f;
        return render(p, in, 512, bpm);
    };
    const Stereo straight = run(0.5f);
    const Stereo swung    = run(0.66f);

    // Onset = first sample after the kick whose magnitude passes a threshold.
    auto onsetMs = [&](const Stereo& s, int beat) {
        const size_t k = (size_t) std::llround(beat * quarter);
        for (size_t i = k; i < k + (size_t) quarter && i < s.l.size(); ++i)
            if (std::fabs(s.l[i]) > 1.0e-3f) return (double) (i - k) * 1000.0 / kSr;
        return -1.0;
    };

    const double div = 0.25 * 60000.0 / bpm;
    const double expectLate = div + (2 * 0.66 - 1) * div;
    double worstTiming = 0.0, worstEnergy = 0.0;
    for (int b = 0; b < beats; ++b) {
        // The reverb has its own onset latency, so compare against the
        // unswung render rather than the bare division.
        const double shift = onsetMs(swung, b) - onsetMs(straight, b);
        worstTiming = std::max(worstTiming, std::fabs(shift - (expectLate - div)));
        const size_t k = (size_t) std::llround(b * quarter);
        // Same-length windows, the swung one offset by the swing shift.
        const size_t shiftSamples = (size_t) std::llround((expectLate - div) * kSr / 1000.0);
        const size_t len = (size_t) quarter - shiftSamples;
        const double e0 = energy(straight.l, k, k + len);
        const double e1 = energy(swung.l,    k + shiftSamples, k + shiftSamples + len);
        worstEnergy = std::max(worstEnergy, std::fabs(e1 / e0 - 1.0));
    }
    check(worstTiming < 0.1, "every kick is shifted by exactly the swing amount (ms err)", worstTiming);
    check(worstEnergy < 0.001, "swing never doubles or drops a kick (energy ratio err)", worstEnergy);
}

// With the timeline handled per sample, the result must not depend on the
// host's buffer size.
void testBlockSizeIndependence() {
    rumble::Params p;
    p.sync = true; p.divBeats = 0.25f; p.swing = 0.6f;
    Stereo in = silence(2.0);
    const double quarter = 60.0 / 128.0 * kSr;
    for (int b = 0; b < 4; ++b) in.l[(size_t) (b * quarter)] = in.r[(size_t) (b * quarter)] = 0.8f;
    const Stereo a = render(p, in, 64, 128.0);
    const Stereo b = render(p, in, 2048, 128.0);
    double diff = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i) diff = std::max(diff, (double) std::fabs(a.l[i] - b.l[i]));
    check(diff < 1.0e-4, "output identical at 64 and 2048 sample buffers (max diff)", diff);
}

// Full duck depth at the threshold, regardless of absolute level.
void testDuckThreshold() {
    auto minGain = [](float levelDb, float threshDb) {
        rumble::Params p = neutral();
        p.duckAmount = 1.0f; p.duckThreshDb = threshDb; p.duckAtkMs = 0.5f;
        rumble::RumbleEngine e;
        e.prepare(kSr, 512);
        e.setParams(p);
        std::vector<float> l(4800, 0.0f), r(4800, 0.0f);
        const float a = std::pow(10.0f, levelDb / 20.0f);
        // A held level: the duck follows the raw trigger, so DC is fine.
        for (int i = 0; i < 2400; ++i) l[(size_t) i] = r[(size_t) i] = a;
        e.process(l.data(), r.data(), (int) l.size());
        return e.lastMinDuckGain();
    };
    const float quietDefault = minGain(-18.0f, -6.0f);
    const float quietMatched = minGain(-18.0f, -18.0f);
    check(quietDefault > 0.5f, "a -18 dB trigger only part-ducks at -6 dB threshold", quietDefault);
    check(quietMatched < 0.05f, "a -18 dB trigger fully ducks at -18 dB threshold", quietMatched);
}

// Pure side-channel sub must be removed by Mono Below.
void testMonoBelowRemovesStereoSub() {
    auto sideLevel = [](float monoHz) {
        rumble::Params p = neutral();
        p.monoBelowHz = monoHz;
        p.decaySec = 1.0f;
        Stereo in = silence(2.0);
        for (size_t i = 0; i < in.l.size(); ++i) {
            const float s = 0.25f * std::sin(2.0f * rumble::kPi * 45.0f * (float) i / kSr);
            in.l[i] = s; in.r[i] = -s;     // all side, no mid
        }
        const Stereo out = render(p, in, 512);
        double e = 0.0;
        for (size_t i = out.l.size() / 2; i < out.l.size(); ++i) {
            const double side = 0.5 * (out.l[i] - out.r[i]);
            e += side * side;
        }
        return 10.0 * std::log10(e + 1e-30);
    };
    const double drop = sideLevel(20.0f) - sideLevel(150.0f);
    check(drop > 25.0, "Mono Below 150 Hz cuts 45 Hz side content by >25 dB", drop);
}

// A big Output step must glide, not jump.
void testOutputGainIsSmoothed() {
    rumble::Params p = neutral();
    p.decaySec = 8.0f;
    Stereo in = silence(1.5);
    for (int i = 0; i < 4800; ++i) in.l[(size_t) i] = in.r[(size_t) i] = 0.5f * std::sin(2.0f * rumble::kPi * 80.0f * (float) i / kSr);
    const size_t stepAt = (size_t) kSr;
    const Stereo out = render(p, in, 512, 0.0, [&](rumble::Params& q, size_t pos) {
        q.outGain = pos >= stepAt ? 0.0625f : 1.0f;   // -24 dB
    });
    // Largest second difference around the step vs. in a quiet stretch
    // before it. A raw step shows up as a big spike.
    auto maxD2 = [&](size_t a, size_t b) {
        double m = 0.0;
        for (size_t i = a + 2; i < b; ++i)
            m = std::max(m, (double) std::fabs(out.l[i] - 2 * out.l[i - 1] + out.l[i - 2]));
        return m;
    };
    const double before = maxD2(stepAt - 4800, stepAt - 10);
    const double around = maxD2(stepAt - 10, stepAt + 200);
    check(around <= before * 1.5, "Output automation glides (step spike / normal)", around / before);
}

} // namespace

int main() {
    testDecayMatchesKnob();
    testBadInputStaysFinite();
    testSwingSchedulesEachKickOnce();
    testBlockSizeIndependence();
    testDuckThreshold();
    testMonoBelowRemovesStereoSub();
    testOutputGainIsSmoothed();
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
