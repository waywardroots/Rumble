# Rumble

A reverb plugin (VST3 + Standalone) built for one job: turning a kick drum into
a techno rumble bass line.

A rumble is a kick fed into a dark, medium-decay reverb whose tail is ducked by
the kick itself, so the low end swells between hits instead of turning to mud.
Rumble does that in one box: dark FDN reverb, feedback low-cut, built-in ducker
(internal or external sidechain trigger), and a mono-below crossover so the sub
stays centred.

## Building

Requires CMake 3.22+ and a C++17 compiler. JUCE 8.0.4 is fetched automatically;
point at an existing checkout with `-DJUCE_PATH=/path/to/JUCE` to skip the
download.

| Format | Windows | macOS | Notes |
|---|---|---|---|
| VST3 | yes | yes | |
| AU | — | yes | macOS-only format |
| AAX | yes | yes | needs the Avid AAX SDK + PACE signing |
| Standalone | yes | yes | for quick testing without a DAW |

### macOS

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Builds a universal binary (arm64 + x86_64, deployment target 10.13). To build
only for your own machine — much faster during development:

```bash
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
```

Ninja or plain Makefiles work too; Xcode is only needed if you want to debug in
it. Note that `CMAKE_BUILD_TYPE` is ignored by Xcode and Visual Studio — those
are multi-config generators, so the config goes on the `--build` line as shown.

### Windows

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Links the static MSVC runtime (`/MT`), so the plugin does not require a
redistributable on the user's machine. For a 32-bit build use `-A Win32`.

### AAX

The AAX SDK is distributed by Avid under NDA and cannot be downloaded by the
build, so AAX is off unless you supply it:

```bash
cmake -B build -DAAX_SDK_PATH=/path/to/aax-sdk
```

The path is validated at configure time. Note that an AAX binary will not load
in Pro Tools until it is signed with a PACE/Avid developer certificate — an
unsigned local build is only usable in Pro Tools Developer builds.

### Install locations

A successful build copies each format into the standard folder:

- macOS VST3: `~/Library/Audio/Plug-Ins/VST3`
- macOS AU: `~/Library/Audio/Plug-Ins/Components`
- Windows VST3: `C:\Program Files\Common Files\VST3` (needs an elevated shell)

Turn that off with `-DRUMBLE_COPY_PLUGIN=OFF` and copy the files yourself.

## Downloading a prebuilt plugin

Every push builds macOS and Windows binaries in GitHub Actions. To grab one:

1. Open the **Actions** tab, pick the newest **Build** run for your branch.
2. Wait for both jobs to go green.
3. Download **Rumble-macOS** or **Rumble-Windows** from the *Artifacts* section
   at the bottom of the run summary.

Artifacts expire after 90 days, and downloading them requires being signed in
to GitHub. Each is a zip containing the plugin zips for that platform.

**Installing on macOS.** Unzip and move `Rumble.vst3` to
`~/Library/Audio/Plug-Ins/VST3` and/or `Rumble.component` to
`~/Library/Audio/Plug-Ins/Components`. The CI build is ad-hoc signed but not
notarised, so macOS quarantines anything downloaded from a browser. Clear it:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Rumble.vst3
```

Without that step the plugin silently fails to appear in your DAW's scan.

**Installing on Windows.** Unzip and move `Rumble.vst3` into
`C:\Program Files\Common Files\VST3`. The build is unsigned, so SmartScreen may
warn on the standalone `.exe`.

AAX is not built in CI — it needs the NDA'd Avid SDK and PACE signing, neither
of which can live in a public workflow.

## Auditioning without a DAW

`tools/offline_render.cpp` synthesises a 135 BPM kick pattern, runs it through
the DSP core and writes a WAV — no JUCE needed:

```bash
g++ -O2 -std=c++17 tools/offline_render.cpp -o rumble_render
./rumble_render demo.wav
```

## Parameters

| Parameter | What it does | Rumble sweet spot |
|---|---|---|
| Mix | Dry/wet | 80–100% on a send, ~50% as an insert |
| Drive | Saturation into the reverb; adds harmonics so the rumble reads on small speakers | 2–4 |
| Pre-Delay | Gap between kick and tail — keeps the transient clean | 8–20 ms |
| Size | Delay-line scaling. Small = tight and dense | 20–40% |
| Decay | RT60. Set by ear against the tempo | 3–6 s |
| Damping | High-cut *inside* the feedback loop; the main "darkness" control | 600–1200 Hz |
| Low Cut | High-pass inside the loop; stops sub build-up | 25–45 Hz |
| Tone | High-cut on the wet output | 250–500 Hz |
| Mod | Slow delay modulation; breaks up metallic ringing | 20–40% |
| Duck | How hard the trigger pushes the tail down | 80–100% |
| Duck Atk / Rel | Ducker envelope. Release sets the pump's groove | 1–3 ms / 150–300 ms |
| Width | Stereo spread of the tail | 100–150% |
| Mono Below | Below this, the tail is forced to mono | 120–180 Hz |
| Output | Final trim | — |

## Usage

**As a send (recommended).** Send the kick to a Rumble bus at 100% Mix. Keep the
dry kick on its own channel. Use the sidechain input fed from the kick so the
tail ducks even if you later feed the reverb from something else.

**As an insert.** Drop it on the kick, Mix around 40–60%.

Tips:
- If it sounds muddy, lower Tone and raise Low Cut before touching Decay.
- Longer Duck Release = more pronounced pumping; match it to your groove.
- Pitching the source kick down before the reverb gives a deeper rumble.

## Layout

- `src/RumbleEngine.h` — the DSP, framework-agnostic and header-only
- `src/PluginProcessor.*` — JUCE wrapper, parameters, sidechain routing
- `src/PluginEditor.*` — knob panel
- `tools/offline_render.cpp` — offline WAV renderer

## How the engine works

1. Input is high-passed and low-passed so only rumble-relevant band enters.
2. `tanh` drive with auto gain compensation.
3. Pre-delay.
4. A 4-line feedback delay network with Hadamard mixing. Per-loop gain comes
   from `g = 10^(-3 * delay / RT60)`. Each feedback path gets a one-pole
   low-pass (Damping) and one-pole high-pass (Low Cut).
5. Delay taps are read fractionally and modulated by slow, uncorrelated LFOs.
6. Wet output is tone-filtered, soft-clipped, and multiplied by the ducker gain.
7. Mid/side width with the side channel high-passed at Mono Below.
