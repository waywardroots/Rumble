// Offline harness: synthesises a 4-on-the-floor kick pattern, runs it through
// RumbleEngine and writes a 16-bit WAV. Lets you audition/verify the DSP
// without a DAW or the plugin SDK.
//
//   g++ -O2 -std=c++17 tools/offline_render.cpp -o rumble_render && ./rumble_render out.wav

#include "../src/RumbleEngine.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr) {
    const uint32_t frames = static_cast<uint32_t>(l.size());
    const uint32_t dataBytes = frames * 2u * 2u;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror("fopen"); return; }

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2);
    u32(static_cast<uint32_t>(sr)); u32(static_cast<uint32_t>(sr) * 4u); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);

    for (uint32_t i = 0; i < frames; ++i) {
        for (float v : { l[i], r[i] }) {
            v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
            u16(static_cast<uint16_t>(static_cast<int16_t>(v * 32767.0f)));
        }
    }
    std::fclose(f);
}

// Simple analogue-style kick: pitch-swept sine + click.
std::vector<float> makeKickPattern(int sr, double bpm, int bars) {
    const int spb = static_cast<int>(60.0 / bpm * sr);
    const int total = spb * 4 * bars;
    std::vector<float> out(static_cast<size_t>(total), 0.0f);

    for (int beat = 0; beat < 4 * bars; ++beat) {
        const int start = beat * spb;
        double phase = 0.0;
        for (int n = 0; n < spb && start + n < total; ++n) {
            const double t = static_cast<double>(n) / sr;
            const double freq = 45.0 + 130.0 * std::exp(-t * 55.0);     // pitch drop
            const double amp  = std::exp(-t * 11.0);                    // body decay
            const double clk  = std::exp(-t * 900.0) * 0.5;             // beater click
            phase += 2.0 * M_PI * freq / sr;
            out[static_cast<size_t>(start + n)] +=
                static_cast<float>(std::tanh(std::sin(phase) * 1.6) * amp + clk);
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : "rumble_demo.wav";
    const int sr = 48000;

    std::vector<float> kick = makeKickPattern(sr, 135.0, 4);
    kick.resize(kick.size() + static_cast<size_t>(sr), 0.0f); // tail room

    std::vector<float> l = kick, r = kick;

    rumble::Params p;
    p.mix = 0.85f; p.drive = 3.0f; p.predelayMs = 12.0f;
    p.size = 0.30f; p.decaySec = 4.0f; p.dampHz = 900.0f; p.lowCutHz = 32.0f;
    p.toneHz = 350.0f; p.modDepth = 0.30f;
    p.duckAmount = 0.9f; p.duckAtkMs = 1.5f; p.duckRelMs = 200.0f;
    p.width = 1.3f; p.monoBelowHz = 150.0f; p.outGain = 0.9f;

    rumble::RumbleEngine eng;
    eng.prepare(sr, 512);
    eng.setParams(p);

    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block) {
        const int n = static_cast<int>(std::min<size_t>(block, l.size() - i));
        eng.process(l.data() + i, r.data() + i, n);
    }

    double peak = 0.0, rms = 0.0;
    for (size_t i = 0; i < l.size(); ++i) {
        peak = std::max(peak, static_cast<double>(std::fabs(l[i])));
        rms += static_cast<double>(l[i]) * l[i];
    }
    rms = std::sqrt(rms / static_cast<double>(l.size()));
    std::printf("frames=%zu peak=%.3f rms=%.3f -> %s\n", l.size(), peak, rms, path.c_str());

    writeWav(path, l, r, sr);
    return 0;
}
