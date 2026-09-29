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

The interface is grouped by function: **DRIVE**, **REVERB** and **TIMING** in
the left column, **FILTER**, **DUCK** and **OUTPUT** in the right.

| Parameter | What it does | Rumble sweet spot |
|---|---|---|
| Drive | Saturation *into* the reverb; thickens the source before it smears | 2–4 |
| Tail Drive | Saturation *after* the reverb. Adds harmonics to the tail so the rumble is audible on speakers with no sub. Saturation compresses peaks, so make up any level with Output rather than expecting Drive to get louder | 2–8, push to 15+ for dirt |
| Pre-Delay | Gap between kick and tail — keeps the transient clean | 8–20 ms |
| Sync | Drive Pre-Delay from the host tempo instead of the ms knob | on, to place the rumble on the grid |
| Division | Beat division used when Sync is on. Straight, dotted (D) and triplet (T) from 1/32 to 1/1 | 1/16 or 1/8 |
| Swing | Delays every second slot of the Division grid. 50% is straight, 66.7% puts the late slot on the triplet, 75% is maximum | 50–58% for a subtle shuffle |
| Size | Delay-line scaling. Small = tight and dense | 20–40% |
| Decay | RT60. Set by ear against the tempo | 3–6 s |
| Damping | High-cut *inside* the feedback loop; the main "darkness" control | 600–1200 Hz |
| Low Cut | High-pass inside the loop; stops sub build-up | 25–45 Hz |
| Tone | High-cut on the wet output | 250–500 Hz |
| Filter Type | Shape of the resonant filter: low pass, band pass or high pass | Low Pass |
| Filter | Cutoff of the resonant filter on the wet signal. 12 dB/oct, sits before the saturator so sweeps stay clean. At 20 kHz it is effectively off | sweep it, or park at 300–600 Hz |
| Resonance | Filter Q. Above ~4 it sings at the cutoff; the saturator downstream keeps the peak in check | 0.7 flat, 3–6 for sweeps |
| Enhance | Distorts only the sub band and adds back just the harmonics it generates, so the rumble reads on speakers with no low end. Does not raise the sub itself | 20–50% |
| Low / Mid / High | Three-band EQ on the wet signal: low shelf at 90 Hz, sweepable peak, high shelf at 2.5 kHz. The only stage here that can *boost* — every other filter cuts | ±3 dB, more if voicing hard |
| Mid Freq | Centre of the peaking band | 300–800 Hz |
| Diffusion | Smears the input through an allpass chain before it reaches the network. Low settings give discrete, slappy echoes; high settings give a smooth wash | 60–80% |
| Mod | Slow delay modulation; breaks up metallic ringing | 20–40% |
| Duck | How hard the trigger pushes the tail down | 80–100% |
| Duck Atk / Rel | Ducker envelope. Release sets the pump's groove | 1–3 ms / 150–300 ms |
| Width | Stereo spread of the tail | 100–150% |
| Mono Below | Below this, the tail is forced to mono | 120–180 Hz |
| Output | Final trim | — |

## Presets

Nine presets ship with the plugin, reachable from the selector in the title bar
and from your host's own preset menu:

| Preset | For |
|---|---|
| Init | Neutral starting point |
| Warehouse | The default big dark rumble |
| Tight Room | Short and controlled, for busy arrangements |
| Sub Roller | Deep and heavily enhanced, weight without mud |
| Basement Distortion | Driven and dirty |
| Offbeat Shuffle | Synced 1/16 with swing |
| Cavern | Very long decay, for breakdowns |
| Filter Sweep | Resonant filter parked ready to automate |
| Clean Tail | Minimal drive, for when the kick already has character |

Loading a preset resets every parameter to its default first, so a preset
always lands in the same place regardless of what was loaded before it.

## Usage

Rumble is built to sit after a drum sampler playing kicks. A techno rumble is a
kick's tail extended with heavy reverb, distortion and filtering until it turns
into a continuous warehouse-style bassline that breathes with the groove.

**Rumble's output is always 100% wet.** There is no dry/wet control: the plugin
converts a kick into a rumble rather than blending the two, and a half-wet
rumble is not a sound anyone wants. That means you must keep the dry kick on a
separate path.

**As a send (recommended).** Send your kick sampler to a Rumble bus and keep
the dry kick on its own channel. The dry kick keeps its transient and punch
untouched, you shape the rumble independently, and the two are balanced with
the bus fader — which is a better tool for the job than a Mix knob.

**On a duplicate track.** Duplicate the kick channel, put Rumble on the copy,
and balance the two faders. Identical result, sometimes easier to automate.

**Do not insert it directly on your only kick channel** — the dry kick would be
replaced by the rumble, transient and all.

### Placing the rumble in the bar

By default the tail starts a few milliseconds after the kick, so the rumble
sits *on* the beat. Turn **Sync** on and pick a **Division** to push the tail
to a musical offset instead — the delay is recalculated from the host tempo
every block, so it stays locked when you change BPM.

- **1/16** — the tail lands between kicks. The standard driving rumble.
- **1/8** — tail arrives on the off-beat, a half-step behind the kick.
- **1/16D / 1/8D** (dotted) — pushes the rumble late for a lurching, swung feel.
- **1/8T / 1/4T** (triplet) — cuts across a straight 4/4 for rolling patterns.
- **1/4 and longer** — the rumble answers the *next* kick rather than its own.
  Interesting with a long Decay, muddy with a short one.

**Swing** shifts every second slot of that grid later, the way an MPC does.
At 50% the grid is even. At 66.7% the late slot sits exactly on the triplet —
the classic shuffle. 75% is the maximum, with the late slot three quarters of
the way through the pair. With Division at 1/16 and Swing around 55%, the
rumble lands fractionally behind every other kick, which is what gives a
straight 4/4 loop its shuffle without touching the drums themselves.

Swing needs the host's timeline position to know which slot it is in, so it
does nothing while the transport is stopped, and nothing when Sync is off.

Sync only moves *when the tail starts*. The pump rhythm is still set by Duck
Release, so adjust the two together: a late Division with a slow Duck Release
will smear the tail into the following kick.

### Dialling in a warehouse rumble

1. On a send or duplicate track, start with Decay ~4 s and Duck 90%.
2. Pull **Tone** down until only weight is left — usually 250–400 Hz. This is
   the single biggest "warehouse" control.
3. Raise **Damping** down to 600–1000 Hz so the tail darkens as it decays.
4. Set **Low Cut** to 30–45 Hz so the tail doesn't fight the kick's fundamental
   or eat all your headroom.
5. Set **Duck Release** by ear against the tempo — the tail should reopen just
   before the next kick lands. This is what makes it groove rather than drone.
6. Add **Tail Drive** last, until the rumble is audible on a laptop speaker.
7. If it still disappears on small speakers, add **Enhance** rather than more
   Tail Drive — it targets the sub band specifically and leaves the weight
   on big systems untouched.

The **Filter** is the one to automate. Park Resonance around 4 and sweep the
cutoff across a breakdown; because it sits before the saturator, the drive
thickens whatever the filter leaves rather than fighting it.

Tips:
- If it sounds muddy, lower Tone and raise Low Cut before touching Decay.
- Longer Duck Release = more pronounced pumping; match it to your groove.
- Pitching the sampler's kick down before the reverb gives a deeper rumble.
- A short, punchy kick sample rumbles more cleanly than a long boomy one — the
  reverb supplies the length, so the source only needs to supply the hit.
- Feed the reverb from a *different* kick than the one you duck with: put the
  sampler's sub-kick into the sidechain input and a clickier layer into the
  main input.

## Robustness

Rumble is a feedback network, so it defends itself against bad input:

- Non-finite samples (NaN/Inf) are rejected at the input, the sidechain and
  the feedback path. One NaN entering a delay network would otherwise
  recirculate forever.
- Parameters are scrubbed before use, and delay-line reads reject non-finite
  delay times. `std::clamp` returns NaN unchanged -- both of its comparisons
  are false -- so a NaN delay time would become `(int)NaN`, which is undefined
  behaviour and indexes the buffer out of bounds. That reads arbitrary memory:
  random loud sparks, or garbage that mutes the ducker.
- The host tempo is validated before it is used to derive a delay time.
- Samples above +18 dBFS are clamped. A stray huge value would otherwise pin
  the duck envelope, muting the output for many seconds while the envelope
  decayed back down.
- The duck envelope is capped, so it always recovers within its release time.
- The output is clamped, so the plugin cannot emit a speaker-damaging spike.

If you hear a tick surviving all of this, the source material genuinely
contains a bad sample and the plugin upstream is worth investigating.

## Layout

- `src/RumbleEngine.h` — the DSP, framework-agnostic and header-only
- `src/PluginProcessor.*` — JUCE wrapper, parameters, sidechain routing
- `src/PluginEditor.*` — knob panel
- `tools/offline_render.cpp` — offline WAV renderer
- `assets/` — brand artwork; see `assets/README.md` for the logo spec

## How the engine works

1. Input is high-passed and low-passed so only rumble-relevant band enters.
2. `tanh` drive with auto gain compensation.
3. Pre-delay.
4. A four-stage allpass diffuser smears the input, so the network is excited
   by a dense cloud rather than a single spike.
5. An 8-line feedback delay network with 8x8 Hadamard mixing, computed as
   three butterfly stages. Line lengths have no simple integer ratios, so
   modes spread instead of stacking into a ringing pitch, and injection
   polarity alternates to decorrelate the lines. Per-loop gain comes from
   `g = 10^(-3 * delay / RT60)`. Each feedback path gets a one-pole low-pass
   (Damping) and one-pole high-pass (Low Cut).
6. Delay taps are read fractionally and modulated by slow, uncorrelated LFOs
   (quadrature oscillators rather than `sin` calls, which matters at 16 taps).
7. Wet output is tone-filtered, resonant-filtered, saturated, enhanced,
   EQ'd, and multiplied by the ducker gain.
8. Mid/side width with the side channel high-passed at Mono Below.

### Why Low Cut is not inside the feedback loop

It used to be, and it made the Decay control lie. A one-pole high-pass at
20 Hz has its pole at 0.9974, which is *slower* than the feedback gain at
every Decay setting (0.66 at 1 s, 0.95 at 4 s). Inside the loop that pole
dominates the decay, so the tail no longer followed the Decay knob: measured
RT60 ran up to 19% short at long settings, and the error changed with the
setting, which is worse than being uniformly wrong.

Low Cut now filters the input to the network instead. Measured RT60 is within
4% of the setting from 0.5 s to 16 s. The knob still shapes the tail -- sweeping
it from 20 Hz to 400 Hz removes about two thirds of the sub energy -- and sub
build-up is actually lower than before (0.85x over 16 bars, against 1.33x when
the filter was in the loop).

Note that Decay is measured at low level. Tail Drive saturates the loud early
part of a tail more than the quiet end, which stretches the measured decay at
realistic levels. That is inherent to a saturating reverb, not a calibration
error: turn Tail Drive down and the decay tightens back up.
