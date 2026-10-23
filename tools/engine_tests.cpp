// Regression tests for the DSP core. Plain C++17, no JUCE, no framework:
//
//   g++ -O2 -std=c++17 tools/engine_tests.cpp -o rumble_tests && ./rumble_tests
//
// CMake builds this as `rumble_tests` and registers it with CTest, and CI runs
// it on every push. Each check prints PASS/FAIL with the measured value, so a
// failure says how far off it was, not just that it was.

#include "../src/RumbleEngine.h"
#include "../src/Lfo.h"

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

// Decay is the RT60 of the band that rings longest -- the peak of the loop
// filters -- at any Damping and Size. Measured with modulation off, which
// on its own shortens long tails a little (see README).
void testDecayMatchesKnob() {
    struct Case { float damp, size, decay; };
    const Case cases[] = {
        { 8000.0f, 0.3f, 1.0f }, { 8000.0f, 0.3f, 4.0f }, { 8000.0f, 0.3f, 10.0f },
        // Dark settings -- what a rumble actually uses. These read 55-80%
        // short when Decay was calibrated at a fixed 400 Hz.
        { 900.0f, 0.1f, 4.0f }, { 900.0f, 0.3f, 4.0f }, { 900.0f, 0.9f, 8.0f }, { 300.0f, 0.3f, 8.0f },
    };
    for (const auto& c : cases) {
        rumble::Params p = neutral();
        p.dampHz = c.damp; p.size = c.size; p.decaySec = c.decay; p.lowCutHz = 32.0f;
        Stereo in = silence(c.decay * 1.6 + 0.5);
        in.l[0] = in.r[0] = 1.0f;
        const Stereo out = render(p, in, 256);
        float peakHz = 0.0f;
        rumble::loopFilterPeak(p.lowCutHz, p.dampHz, (float) kSr, &peakHz);
        rumble::SVF band;
        band.set(peakHz, 5.0f, (float) kSr);
        std::vector<float> ir(out.l.size());
        for (size_t i = 0; i < ir.size(); ++i) ir[i] = band.process(out.l[i], rumble::SVF::BandPass);
        const double measured = measureRt60(ir);
        const double err = std::fabs(measured - c.decay) / c.decay;
        char what[120];
        std::snprintf(what, sizeof what, "Decay %g s accurate at Damping %g Hz, Size %g%% (s)",
                      (double) c.decay, (double) c.damp, (double) c.size * 100.0);
        check(err < 0.10, what, measured);
    }
}

// The loop gain is capped at its peak, so no setting can make it run away --
// not even Low Cut above Damping with the longest Decay.
void testExtremeSettingsStayStable() {
    rumble::Params p = neutral();
    p.lowCutHz = 400.0f; p.dampHz = 100.0f; p.decaySec = 30.0f; p.size = 1.0f;
    Stereo in = silence(20.0);
    for (size_t i = 0; i < 4800; ++i) in.l[i] = in.r[i] = 0.5f * std::sin(0.05f * (float) i);
    const Stereo out = render(p, in, 512);
    float peak = 0.0f;
    bool finite = true;
    for (float v : out.l) { peak = std::max(peak, std::fabs(v)); finite &= std::isfinite(v); }
    const double late = energy(out.l, out.l.size() - (size_t) kSr, out.l.size());
    const double early = energy(out.l, (size_t) kSr, (size_t) (2 * kSr));
    check(finite && peak < 2.0f && late < early, "crossed Low Cut / Damping at 30 s Decay stays stable (peak)", peak);
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

// ------------------------------------------------------------- echo delay

// A single kick, rendered with the echo on and off. With ducking off and the
// echo added after everything nonlinear, (on - off) is exactly the echo.
struct EchoRender { Stereo on, off; };
EchoRender renderEcho(rumble::Params p, double seconds, double bpm = 0.0) {
    Stereo in = silence(seconds);
    in.l[0] = in.r[0] = 1.0f;
    rumble::Params off = p;
    off.delayMix = 0.0f;
    return { render(p, in, 512, bpm), render(off, in, 512, bpm) };
}

size_t firstAbove(const std::vector<float>& x, float threshold, size_t from = 0) {
    for (size_t i = from; i < x.size(); ++i)
        if (std::fabs(x[i]) > threshold) return i;
    return x.size();
}

rumble::Params echoBase() {
    rumble::Params p = neutral();
    p.decaySec = 0.2f;
    p.diffusion = 0.0f;
    p.delayMix = 1.0f;
    p.delayFeedback = 0.0f;
    return p;
}

void testEchoOffChangesNothing() {
    rumble::Params a;            // defaults: delay off
    rumble::Params b = a;
    b.delayMs = 900.0f; b.delayFeedback = 0.9f; b.delayPingPong = true;
    Stereo in = silence(2.0);
    std::mt19937 g(3);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    for (size_t i = 0; i < 9600; ++i) in.l[i] = in.r[i] = u(g);
    const Stereo x = render(a, in, 512), y = render(b, in, 512);
    double diff = 0.0;
    for (size_t i = 0; i < x.l.size(); ++i) diff = std::max(diff, (double) std::fabs(x.l[i] - y.l[i]));
    check(diff == 0.0, "Delay at 0% leaves the output bit-identical", diff);
}

void testEchoTiming() {
    // Milliseconds, and the same time reached via tempo sync.
    for (int synced = 0; synced < 2; ++synced) {
        rumble::Params p = echoBase();
        p.delayMs = 250.0f;
        p.sync = synced != 0;
        p.delayBeats = 0.5f;                     // 1/8 at 120 BPM = 250 ms
        const auto r = renderEcho(p, 1.5, synced ? 120.0 : 0.0);
        std::vector<float> echoOnly(r.on.l.size());
        for (size_t i = 0; i < echoOnly.size(); ++i) echoOnly[i] = r.on.l[i] - r.off.l[i];
        const size_t tail = firstAbove(r.off.l, 1.0e-3f);
        const size_t rep  = firstAbove(echoOnly, 1.0e-3f);
        const double gapMs = ((double) rep - (double) tail) * 1000.0 / kSr;
        check(std::fabs(gapMs - 250.0) < 0.5,
              synced ? "synced 1/8 at 120 BPM repeats 250 ms after the tail (ms)"
                     : "250 ms echo repeats 250 ms after the tail (ms)", gapMs);
    }
}

void testEchoFeedbackStaysTame() {
    rumble::Params p;
    p.delayMix = 1.0f; p.delayFeedback = rumble::kMaxEchoFeedback; p.delayMs = 120.0f;
    p.tailDrive = 15.0f; p.toneHz = 3000.0f; p.lowCutHz = 20.0f;
    Stereo in = silence(14.0);
    std::mt19937 g(9);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (size_t i = 0; i < (size_t) (2 * kSr); ++i) { in.l[i] = u(g); in.r[i] = u(g); }
    const Stereo out = render(p, in, 512);
    bool finite = true;
    float peak = 0.0f;
    for (size_t i = 0; i < out.l.size(); ++i) {
        finite &= std::isfinite(out.l[i]) && std::isfinite(out.r[i]);
        peak = std::max(peak, std::fabs(out.l[i]));
    }
    const double early = energy(out.l, 2 * kSr, 3 * kSr);
    const double late  = energy(out.l, 13 * kSr, 14 * kSr);
    check(finite && peak < 4.0f, "max feedback + heavy drive stays finite and bounded (peak)", peak);
    check(late < early * 0.01, "max feedback still dies away (last s / first s energy)", late / early);
}

void testPingPongAlternates() {
    rumble::Params p = echoBase();
    p.delayMs = 200.0f; p.delayFeedback = 0.6f; p.delayPingPong = true;
    p.width = 1.0f;
    const auto r = renderEcho(p, 1.5);
    // Energy of the echo in each repeat's window, per side.
    const size_t tail = firstAbove(r.off.l, 1.0e-3f);
    const size_t step = (size_t) (0.2 * kSr);
    auto side = [&](int repeat, bool left) {
        const size_t a = tail + (size_t) repeat * step, b = a + step;
        double e = 0.0;
        for (size_t i = a; i < b && i < r.on.l.size(); ++i) {
            const double d = left ? r.on.l[i] - r.off.l[i] : r.on.r[i] - r.off.r[i];
            e += d * d;
        }
        return e;
    };
    // Mid/side folding mixes a little across; demand a clear majority.
    const double first  = side(1, true) / (side(1, false) + 1e-30);
    const double second = side(2, false) / (side(2, true) + 1e-30);
    check(first > 4.0 && second > 4.0, "ping-pong: 1st repeat left, 2nd right (min ratio)", std::min(first, second));
}

// A long delay line read at a fractional delay must stay exact across the
// wrap. Computing the read position in float once rounded it one past the
// end of a 4 s buffer and produced full-scale spikes from the echo.
void testLongDelayLineWraps() {
    const int n = (int) (rumble::kMaxEchoMs * 0.001f * kSr) + 8;
    rumble::DelayLine d;
    d.prepare(n);
    double worst = 0.0;
    for (long t = 0; t < (long) n * 2 + 50000; ++t) {
        const float v = (float) std::sin(0.001 * (double) t);
        for (float delay : { 18000.002f, 18000.5f, 2.25f, (float) n - 2.5f }) {
            const float y = d.read(delay);
            const long back = (long) delay;
            const double frac = (double) delay - (double) back;
            if (t - back - 1 < 0) continue;
            const double a = std::sin(0.001 * (double) (t - back));
            const double b = std::sin(0.001 * (double) (t - back - 1));
            worst = std::max(worst, std::fabs((double) y - (a + frac * (b - a))));
        }
        d.push(v);
    }
    check(worst < 1.0e-4, "4 s delay line: fractional reads exact across the wrap (max err)", worst);
}

// The whole echo, run well past its buffer length with plugin-like settings.
void testEchoLongRunStaysClean() {
    rumble::Params p;   // engine defaults, drive and ducking on
    p.delayMix = 1.0f; p.delayFeedback = 0.6f; p.delayMs = 375.0f;
    const double bpm = 128.0, q = 60.0 / bpm * kSr;
    Stereo in = silence(12.0);
    for (int b = 0; b * q < (double) in.l.size(); ++b) {
        const size_t k = (size_t) (b * q);
        for (size_t i = 0; i < 4000 && k + i < in.l.size(); ++i)
            in.l[k + i] = in.r[k + i] = 0.5f * std::sin(0.02f * (float) i) * std::exp(-(float) i / 900.0f);
    }
    const Stereo out = render(p, in, 512, bpm);
    float peak = 0.0f;
    for (size_t i = 0; i < out.l.size(); ++i)
        peak = std::max(peak, std::max(std::fabs(out.l[i]), std::fabs(out.r[i])));
    check(peak < 1.0f, "echo at 100% for 12 s (3x its buffer) never clips (peak)", peak);
}

// ------------------------------------------------------------ wet / dry

Stereo kickTrain(double seconds) {
    Stereo in = silence(seconds);
    const double q = 60.0 / 128.0 * kSr;
    for (int b = 0; b * q < (double) in.l.size(); ++b) {
        const size_t k = (size_t) (b * q);
        for (size_t i = 0; i < 6000 && k + i < in.l.size(); ++i)
            in.l[k + i] = in.r[k + i] = 0.6f * std::sin(0.015f * (float) i) * std::exp(-(float) i / 1200.0f);
    }
    return in;
}

double maxDiff(const Stereo& a, const Stereo& b) {
    double d = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i)
        d = std::max(d, (double) std::max(std::fabs(a.l[i] - b.l[i]), std::fabs(a.r[i] - b.r[i])));
    return d;
}

// At 0% a section must vanish: changing its settings changes nothing.
void testMixAtZeroBypassesSection() {
    const Stereo in = kickTrain(3.0);
    struct Case { const char* what; void (*off)(rumble::Params&); void (*a)(rumble::Params&); void (*b)(rumble::Params&); };
    const Case cases[] = {
        { "DRIVE mix 0%: drive, tail drive, enhance have no effect",
          [](rumble::Params& p) { p.driveMix = 0.0f; },
          [](rumble::Params& p) { p.drive = 1.0f; p.tailDrive = 1.0f; p.enhance = 0.0f; },
          [](rumble::Params& p) { p.drive = 9.0f; p.tailDrive = 18.0f; p.enhance = 1.0f; } },
    };
    for (const auto& c : cases) {
        rumble::Params pa, pb;
        c.off(pa); c.off(pb); c.a(pa); c.b(pb);
        const double d = maxDiff(render(pa, in, 512), render(pb, in, 512));
        check(d == 0.0, c.what, d);
    }

    // REVERB at 0% is the untouched input.
    rumble::Params p;
    p.reverbMix = 0.0f;
    const Stereo out = render(p, in, 512);
    check(maxDiff(out, in) == 0.0, "REVERB mix 0%: output is exactly the dry input", maxDiff(out, in));

    // And in between, both are present.
    p.reverbMix = 0.5f;
    const Stereo half = render(p, in, 512);
    rumble::Params wetOnly;
    const Stereo wet = render(wetOnly, in, 512);
    double err = 0.0;
    for (size_t i = 0; i < in.l.size(); ++i)
        err = std::max(err, (double) std::fabs(half.l[i] - 0.5f * (in.l[i] + wet.l[i])));
    check(err < 1.0e-5, "REVERB mix 50%: half dry kick, half rumble (max err)", err);
}

// ------------------------------------------------------ section switches

// A switched-off section passes audio through untouched, so none of its
// settings may change the output.
void testSectionSwitchesBypass() {
    const Stereo in = kickTrain(3.0);
    using Fn = void (*)(rumble::Params&);
    struct Case { const char* what; Fn off; Fn a; Fn b; };
    const Case cases[] = {
        { "DRIVE off: drive, tail drive, enhance have no effect",
          [](rumble::Params& p) { p.driveOn = false; },
          [](rumble::Params& p) { p.drive = 1.0f; p.tailDrive = 1.0f; p.enhance = 0.0f; },
          [](rumble::Params& p) { p.drive = 9.0f; p.tailDrive = 18.0f; p.enhance = 1.0f; } },
        { "REVERB off: size, decay, damping, diffusion have no effect",
          [](rumble::Params& p) { p.reverbOn = false; },
          [](rumble::Params& p) { p.size = 0.0f; p.decaySec = 0.5f; p.dampHz = 300.0f; p.diffusion = 0.0f; },
          [](rumble::Params& p) { p.size = 1.0f; p.decaySec = 20.0f; p.dampHz = 8000.0f; p.diffusion = 1.0f; } },
        { "TIMING off: pre-delay, sync, swing have no effect",
          [](rumble::Params& p) { p.timingOn = false; },
          [](rumble::Params& p) { p.predelayMs = 0.0f; },
          [](rumble::Params& p) { p.predelayMs = 150.0f; p.sync = true; p.swing = 0.7f; } },
        { "DELAY off: level, time, feedback have no effect",
          [](rumble::Params& p) { p.delayOn = false; },
          [](rumble::Params& p) { p.delayMix = 0.0f; },
          [](rumble::Params& p) { p.delayMix = 1.0f; p.delayFeedback = 0.9f; p.delayPingPong = true; } },
        { "FILTER off: tone, cutoff, resonance have no effect",
          [](rumble::Params& p) { p.filterOn = false; },
          [](rumble::Params& p) { p.toneHz = 5000.0f; p.filterHz = 20000.0f; },
          [](rumble::Params& p) { p.toneHz = 80.0f; p.filterHz = 150.0f; p.filterQ = 10.0f; } },
        { "EQ off: EQ gains have no effect",
          [](rumble::Params& p) { p.eqOn = false; },
          [](rumble::Params& p) { p.eqLowDb = 0.0f; },
          [](rumble::Params& p) { p.eqLowDb = 12.0f; p.eqMidDb = -12.0f; p.eqHighDb = 12.0f; } },
        { "DUCK off: duck amount and threshold have no effect",
          [](rumble::Params& p) { p.duckOn = false; },
          [](rumble::Params& p) { p.duckAmount = 0.0f; },
          [](rumble::Params& p) { p.duckAmount = 1.0f; p.duckThreshDb = -40.0f; } },
        { "OUTPUT off: width, mono below, output trim have no effect",
          [](rumble::Params& p) { p.outputOn = false; },
          [](rumble::Params& p) { p.width = 0.0f; p.monoBelowHz = 20.0f; p.outGain = 0.1f; },
          [](rumble::Params& p) { p.width = 2.0f; p.monoBelowHz = 900.0f; p.outGain = 3.0f; } },
    };
    for (const auto& c : cases) {
        rumble::Params pa, pb;
        c.off(pa); c.off(pb); c.a(pa); c.b(pb);
        const double d = maxDiff(render(pa, in, 512), render(pb, in, 512));
        check(d < 1.0e-6, c.what, d);
    }

    // Switching mid-stream crossfades instead of clicking.
    rumble::Params p;
    p.eqLowDb = 18.0f; p.outGain = 2.0f;
    const size_t at = (size_t) kSr;
    const Stereo out = render(p, in, 512, 0.0, [&](rumble::Params& q, size_t pos) {
        q.eqOn = pos < at; q.outputOn = pos < at;
    });
    double jump = 0.0, typical = 0.0;
    for (size_t i = 1; i < out.l.size(); ++i) {
        const double d = std::fabs(out.l[i] - out.l[i - 1]);
        if (i >= at && i < at + 32) jump = std::max(jump, d); else typical = std::max(typical, d);
    }
    check(jump <= typical, "switching sections off mid-stream does not click (step / max normal step)", jump / typical);
}

// ----------------------------------------------------------- key tuning

// Power at `hz` over a stretch of signal (Goertzel, Hann-windowed).
double powerAt(const std::vector<float>& x, size_t from, size_t len, double hz) {
    const double w = 2.0 * 3.14159265358979 * hz / kSr, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < len; ++i) {
        const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * (double) i / (double) (len - 1));
        const double s0 = x[from + i] * win + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

struct TuneResult { double onKey; double strongestHz; double rmsDb; };

// Plugin-like defaults, a kick train, and where the tail's bass energy lands.
TuneResult tuneResult(float tune, int key, double rootHz) {
    rumble::Params p;
    p.drive = 3.0f; p.decaySec = 4.0f; p.dampHz = 900.0f; p.lowCutHz = 32.0f;
    p.toneHz = 350.0f; p.width = 1.3f; p.monoBelowHz = 150.0f; p.size = 0.3f;
    p.tune = tune; p.tuneKey = key;
    const Stereo out = render(p, kickTrain(8.0), 512);
    const size_t from = (size_t) (2 * kSr), len = (size_t) (5 * kSr);
    double total = 0.0, on = 0.0, best = 0.0, bestHz = 0.0;
    for (double hz = 25.0; hz <= 260.0; hz += 0.5) {
        const double pw = powerAt(out.l, from, len, hz);
        total += pw;
        bool near = false;
        for (int h = 1; h <= rumble::kTuneHarmonics; ++h) near = near || std::fabs(hz - h * rootHz) <= 2.5;
        if (near) on += pw;
        if (pw > best) { best = pw; bestHz = hz; }
    }
    return { on / total, bestHz, 10.0 * std::log10(energy(out.l, from, from + len) / (double) len) };
}

bool onHarmonic(double hz, double rootHz) {
    for (int h = 1; h <= rumble::kTuneHarmonics; ++h)
        if (std::fabs(hz - h * rootHz) <= 2.0) return true;
    return false;
}

void testTuneOffIgnoresKey() {
    const Stereo in = kickTrain(2.0);
    rumble::Params a, b;
    a.tune = b.tune = 0.0f;
    a.tuneKey = 0; b.tuneKey = 7;
    const double d = maxDiff(render(a, in, 512), render(b, in, 512));
    check(d == 0.0, "Tune 0%: the Key has no effect at all", d);
}

void testTunePullsTailOntoKey() {
    // A and E share no harmonics in this range, so a move is unambiguous.
    const double a1 = rumble::tuneRootHz(9), e1 = rumble::tuneRootHz(4);
    const auto off = tuneResult(0.0f, 9, a1);
    const auto onA = tuneResult(1.0f, 9, a1);
    const auto onE = tuneResult(1.0f, 4, e1);
    check(off.onKey < 0.1 && onA.onKey > 0.5,
          "Tune 100% (A): most bass energy on A's first 4 harmonics (fraction)", onA.onKey);
    check(onHarmonic(onA.strongestHz, a1), "Tune 100% (A): the loudest bass frequency is in key (Hz)", onA.strongestHz);
    check(onHarmonic(onE.strongestHz, e1) && ! onHarmonic(onE.strongestHz, a1) && onE.onKey > 0.4,
          "changing Key to E moves the pitch to E's harmonics (Hz)", onE.strongestHz);
    check(std::fabs(onA.rmsDb - off.rmsDb) < 3.0,
          "Tune changes pitch content, not volume (dB change at 100%)", onA.rmsDb - off.rmsDb);
}

// ----------------------------------------------------------------- LFOs

void testLfoFreeRate() {
    rumble::Lfo lfo;
    lfo.prepare(kSr);
    rumble::Lfo::Settings s;
    s.rateHz = 2.0;
    // Time between upward zero crossings of a 2 Hz sine, in 32-sample steps.
    float prev = lfo.next(s, 32, false, 0.0, 120.0);
    std::vector<double> ups;
    for (int i = 1; i < kSr * 3 / 32; ++i) {
        const float v = lfo.next(s, 32, false, 0.0, 120.0);
        if (prev < 0.0f && v >= 0.0f) ups.push_back(i * 32.0 / kSr);
        prev = v;
    }
    const double period = ups.size() >= 2 ? (ups.back() - ups.front()) / (double) (ups.size() - 1) : 0.0;
    check(std::fabs(period - 0.5) < 0.002, "2 Hz LFO has a 0.5 s period (s)", period);
}

void testLfoLocksToBar() {
    rumble::Lfo a, b;
    a.prepare(kSr); b.prepare(kSr);
    rumble::Lfo::Settings s;
    s.sync = true; s.cycleBeats = 4.0;   // one cycle per bar
    // Two LFOs with different histories agree once the host reports a position.
    for (int i = 0; i < 1000; ++i) a.next(s, 32, false, 0.0, 97.0);
    const float va = a.next(s, 32, true, 9.0, 128.0);   // bar 3, beat 2: a quarter cycle
    const float vb = b.next(s, 32, true, 9.0, 128.0);
    check(va == vb && std::fabs(va - 1.0f) < 1.0e-5f, "synced LFO is locked to the bar (sine at 1/4 cycle)", va);
}

void testLfoRandomIsRepeatable() {
    rumble::Lfo::Settings s;
    s.shape = rumble::LfoShape::SampleHold;
    s.sync = true; s.cycleBeats = 0.25;
    auto run = [&](std::vector<float>& out) {
        rumble::Lfo lfo(7);
        lfo.prepare(kSr);
        for (int i = 0; i < 2000; ++i) out.push_back(lfo.next(s, 32, true, i * 32.0 * 128.0 / 60.0 / kSr, 128.0));
    };
    std::vector<float> x, y;
    run(x); run(y);
    int changes = 0;
    for (size_t i = 1; i < x.size(); ++i) changes += x[i] != x[i - 1];
    // 2000 blocks of 32 samples is 1.33 s: 2.84 beats at 128 BPM, so the
    // value should step 11 times (once per sixteenth).
    check(x == y && changes == 11,
          "synced sample & hold: steps once per cycle, same pattern every play", changes);
}

void testLfoShapesBounded() {
    bool ok = true;
    for (int shape = 0; shape < rumble::kNumLfoShapes; ++shape) {
        rumble::Lfo lfo(3);
        lfo.prepare(kSr);
        rumble::Lfo::Settings s;
        s.shape = (rumble::LfoShape) shape;
        s.rateHz = 7.3;
        for (int i = 0; i < 20000; ++i) {
            const float v = lfo.next(s, 32, false, 0.0, 120.0);
            ok &= std::isfinite(v) && v >= -1.0f && v <= 1.0f;
        }
    }
    check(ok, "every LFO shape stays finite and within -1..1", ok ? 1.0 : 0.0);

    const float depths[2] = { 0.8f, 0.5f };
    const float lfos[2] = { 1.0f, 1.0f };
    check(rumble::modulateNormalised(0.6f, depths, lfos, 2) == 1.0f,
          "modulated knob position clamps to its range", rumble::modulateNormalised(0.6f, depths, lfos, 2));
}

} // namespace

int main() {
    testDecayMatchesKnob();
    testExtremeSettingsStayStable();
    testBadInputStaysFinite();
    testSwingSchedulesEachKickOnce();
    testBlockSizeIndependence();
    testDuckThreshold();
    testMonoBelowRemovesStereoSub();
    testOutputGainIsSmoothed();
    testEchoOffChangesNothing();
    testEchoTiming();
    testEchoFeedbackStaysTame();
    testPingPongAlternates();
    testLongDelayLineWraps();
    testEchoLongRunStaysClean();
    testMixAtZeroBypassesSection();
    testSectionSwitchesBypass();
    testTuneOffIgnoresKey();
    testTunePullsTailOntoKey();
    testLfoFreeRate();
    testLfoLocksToBar();
    testLfoRandomIsRepeatable();
    testLfoShapesBounded();
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
