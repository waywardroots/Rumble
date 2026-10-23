# Rumble

A reverb plugin (VST3 + Standalone) built for one job: turning a kick drum into
a techno rumble bass line.

A rumble is a kick fed into a dark, medium-decay reverb whose tail is ducked by
the kick itself, so the low end swells between hits instead of turning to mud.
Rumble does that in one box: dark FDN reverb, feedback low-cut, built-in ducker
(internal or external sidechain trigger), and a mono-below crossover so the sub
stays centred.

## Building

Requires CMake 3.22+ and a C++17 compiler. JUCE 9.0.4 is fetched automatically;
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

Tagged versions are published on the **Releases** page, with a zip per format
and platform. Those never expire and need no GitHub account.

To release, bump the version in `CMakeLists.txt`, then push a matching tag:

```bash
git tag v0.22.0 && git push origin v0.22.0
```

The workflow refuses to publish if the tag and the CMake version disagree.

Every push also builds macOS and Windows binaries. To grab an untagged build:

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

## Tests

`tools/engine_tests.cpp` checks the DSP core against measured numbers:
Decay calibration at 400 Hz, swing timing (every kick shifted by exactly the
swing amount, none doubled or dropped), identical output at any buffer size,
duck threshold, how much stereo sub Mono Below removes, Output smoothing,
NaN/Inf rejection, every section switch making that section's settings
irrelevant (and switching without a click), the echo delay (timing in ms and synced, ping-pong,
stability at maximum feedback, no effect at all when off), every Dry/Wet at
0% making its section vanish, and the LFOs (rate, bar-locking, repeatable
random, bounded output). `tools/plugin_tests.cpp` runs the real plugin: an
LFO audibly moving a knob, assignments surviving a session reload, presets
setting and clearing them. It is built with the plugin and runs through CTest:

```bash
ctest --test-dir build -C Release --output-on-failure
```

The engine tests also build on their own, without JUCE:

```bash
g++ -O2 -std=c++17 tools/engine_tests.cpp -o rumble_tests && ./rumble_tests
```

CI runs these on every push, then validates the built VST3 (and the AU on
macOS) with [pluginval](https://github.com/Tracktion/pluginval) at its
strictest level, which exercises state save/restore, automation, odd buffer
sizes and sample rates, and opening the editor.

## Interface

Across the top, a live display scrolls the last few seconds: the **kick**
coming in (grey bars), the **rumble** going out (amber), and the **duck**
gain reduction hanging from the top edge (white), with the current output
peak and gain reduction in the corner. It is the quickest way to set Duck
Threshold and Release — the tail should dip as each kick lands and swell
back before the next one.

Below it, the controls sit on plates grouped by function. The lamp and name
at the top of each plate is its **on/off switch**: click it to switch the
section off (the lamp goes dark and the controls dim, but stay adjustable),
click again to switch it back on. Switching crossfades over a few
milliseconds, so it never clicks, and the switches are ordinary parameters —
your host can automate them, and presets and sessions remember them.

| Switched off | What happens |
|---|---|
| DRIVE | No saturation: Input Drive, Tail Drive and Enhance are bypassed |
| REVERB | No tail: the pre-delayed input passes straight on, unfiltered by Low Cut and Damping. With DUCK still on, the ducker then ducks the kick itself |
| TIMING | No pre-delay, and Sync and Swing are ignored |
| DELAY | No echo |
| FILTER | Tone and the resonant filter are bypassed |
| EQ | Flat |
| DUCK | No ducking |
| OUTPUT | Width back to 100%, no mono-below, Output trim at 0 dB |
| LFO 1 / LFO 2 | The LFO stops moving its knobs (they sit at their own settings); its assignments are kept |

Controls that
have no effect in the current mode are greyed out (Pre-Delay and the delay's Time
while Sync is on; Division, Swing and the delay's Division while it is off).

The window can be resized from the bottom-right corner, from 70% to 200%.
Everything scales together, and the session remembers the size.

The preset bar has arrows to step through every preset, factory then user.

## Parameters

The interface is grouped by function: **DRIVE**, **REVERB**, **TIMING** and
**DELAY** in the left column, **FILTER**, **EQ**, **DUCK** and **OUTPUT** in the
right, and the two **LFO**s across the bottom. The DELAY plate's Time and
Division follow the Sync switch in TIMING.

| Parameter | What it does | Rumble sweet spot |
|---|---|---|
| Drive | Saturation *into* the reverb; thickens the source before it smears | 2–4 |
| Tail Drive | Saturation *after* the reverb. Adds harmonics to the tail so the rumble is audible on speakers with no sub. Saturation compresses peaks, so make up any level with Output rather than expecting Drive to get louder | 2–8, push to 15+ for dirt |
| Pre-Delay | Gap between kick and tail — keeps the transient clean. Greyed out while Sync is on | 8–20 ms |
| Sync | Drive Pre-Delay from the host tempo instead of the ms knob | on, to place the rumble on the grid |
| Division | Beat division used when Sync is on. Straight, dotted (D) and triplet (T) from 1/32 to 1/1 | 1/16 or 1/8 |
| Swing | Delays tails that land on every second slot of the Division grid. 50% is straight, 66.7% puts the late slot on the triplet, 75% is maximum. Needs Sync and a running transport | 50–58% for a subtle shuffle |
| Delay Level | Level of the echo repeats added to the tail. 0% switches the echo off entirely | 20–50% |
| Delay Time | Echo time in ms when Sync is off. Greyed out while Sync is on | 250–500 ms |
| Delay Division | Echo time as a beat division when Sync is on. Greyed out while Sync is off | 1/8D (dub), 1/4 or 1/8 |
| Feedback | How much of each repeat feeds the next, so how many repeats you hear | 30–50% |
| Ping-Pong | Repeats alternate left and right | on for width, off to keep the echo centred |
| Size | Delay-line scaling. Small = tight and dense | 20–40% |
| Decay | RT60. Set by ear against the tempo | 3–6 s |
| Damping | High-cut *inside* the feedback loop; the main "darkness" control | 600–1200 Hz |
| Low Cut | High-pass on the reverb's input and inside its feedback loop; stops sub build-up | 25–45 Hz |
| Tone | High-cut on the wet output | 250–500 Hz |
| Filter Type | Shape and slope of the resonant filter: Low Pass 12/24/48, High Pass 12/24/48, Band Pass 12/24, Notch, Deep Notch, All Pass | Low Pass 24 for sweeps |
| Filter | Cutoff of the resonant filter on the wet signal. Sits before the saturator so sweeps stay clean. At 20 kHz a low pass is effectively off | sweep it, or park at 300–600 Hz |
| Resonance | Filter Q. At 0.707 the low/high pass responses are maximally flat (Butterworth); above that they sing at the cutoff, up to about +21 dB. The saturator downstream keeps the peak in check | 0.7 flat, 3–6 for sweeps |
| Enhance | Distorts only the sub band and adds back just the harmonics it generates, so the rumble reads on speakers with no low end. Does not raise the sub itself | 20–50% |
| Weight | Low shelf at 90 Hz. Sub weight — you feel this more than you hear it | ±3 dB |
| Body | Peaking band, the most audible of the three. Cut here to clear congestion, boost for thickness | ±4 dB |
| Body Freq | Centre of the peaking band | 150–350 Hz |
| Edge | High shelf at 350 Hz. Definition and grit at the top of the rumble band | ±3 dB |
| Diffusion | Smears the input through an allpass chain before it reaches the network. Low settings give discrete, slappy echoes; high settings give a smooth wash | 60–80% |
| Mod | Depth of the delay modulation — *how far* the tail drifts. Also breaks up metallic ringing | 20–40% |
| Mod Rate | Speed of that drift — *how often* the character shifts. 1x moves on a 10–25 s cycle; above 3x it becomes an audible wobble, and 8x is chorus | 1x, lower for long tails |
| Duck | How hard the trigger pushes the tail down | 80–100% |
| Duck Threshold | Trigger level that reaches full duck depth. Quieter triggers duck proportionally less, so set it near your kick's peak level | kick peak, e.g. −12 to −6 dB |
| Duck Atk / Rel | Ducker envelope. Release sets the pump's groove | 1–3 ms / 150–300 ms |
| Trigger | What drives the ducker: **Input** (the plugin's own input, the default), **Sidechain**, or **Auto** (sidechain while it carries signal, otherwise input) | Input, or Sidechain for a separate trigger |
| Dry/Wet (DRIVE) | Blends clean and saturated, for Input Drive, Tail Drive and Enhance together — parallel saturation | 100%, or 40–70% to keep some clean body |
| Dry/Wet (REVERB) | Blends the untouched input with the finished rumble. Below 100% the dry kick is in the output, so only use it when Rumble is on its own track, not on a send | 100% on a send |
| Width | Stereo spread of the tail | 100–150% |
| Mono Below | Below this, the tail's side channel is removed with a 24 dB/oct filter, so the sub stays centred | 120–180 Hz |
| Output | Final trim | — |

## Presets

Eleven presets ship with the plugin, reachable from the selector in the title bar
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
| Dotted Echo | Synced dotted-1/8 ping-pong echo on the tail |
| Breathing Sweep | LFO 1 sweeps the filter over two bars; LFO 2 drifts the width at random |

Loading a factory preset resets every parameter to its default first, so a
preset always lands in the same place regardless of what was loaded before it.

The selector always shows the preset you loaded, with a `*` once you have
changed anything. Pick the same preset again to throw your changes away. The
session remembers both the name and the `*`, so they survive closing the
plugin window and reopening the project.

### Your own presets

**Save** next to the selector stores the current settings under a name you
choose. Saved presets appear in a **User** section at the bottom of the menu,
and **Del** removes the selected one (factory presets cannot be deleted).

They are plain XML files, one per preset, in:

| | |
|---|---|
| macOS | `~/Library/Application Support/Fuzzy Audio/Rumble/Presets` |
| Windows | `%APPDATA%\Fuzzy Audio\Rumble\Presets` |

Being ordinary files, they survive reinstalling the plugin and can be copied
between machines or checked into version control. Characters the filesystem
will not accept are replaced in the filename, but the menu still shows the
name you typed — it is stored inside the file.

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
to a musical offset instead — the delay follows the host tempo sample by
sample, so it stays locked when you change BPM and does not depend on your
buffer size. Changes of delay time (automation, tempo changes, swing) are
crossfaded over 3 ms rather than jumping, so they never click.

- **1/16** — the tail lands between kicks. The standard driving rumble.
- **1/8** — tail arrives on the off-beat, a half-step behind the kick.
- **1/16D / 1/8D** (dotted) — pushes the rumble late for a lurching, swung feel.
- **1/8T / 1/4T** (triplet) — cuts across a straight 4/4 for rolling patterns.
- **1/4 and longer** — the rumble answers the *next* kick rather than its own.
  Interesting with a long Decay, muddy with a short one.

**Swing** shifts every second slot of that grid later, the way an MPC does.
At 50% the grid is even. At 66.7% the late slot sits exactly on the triplet —
the classic shuffle. 75% is the maximum, with the late slot three quarters of
the way through the pair.

What gets delayed is a *tail that lands on a late slot*, and the slot is
decided when the kick goes in, so each kick's tail is delayed as a whole —
never cut in two or played twice. With a four-on-the-floor kick and Division
at 1/16 or 1/8, every kick's tail lands on an off-beat slot, so Swing pushes
all of them back by the same amount: a lazier, laid-back rumble. Every
*other* kick is affected only when kicks fall on alternating slots — Division
1/4 with kicks on every beat, or a broken kick pattern at 1/16.

Swing needs the host's timeline position to know which slot it is in, so it
does nothing while the transport is stopped, and nothing when Sync is off.

Sync only moves *when the tail starts*. The pump rhythm is still set by Duck
Release, so adjust the two together: a late Division with a slow Duck Release
will smear the tail into the following kick.

### LFOs

Two LFOs can move any knob — drag from an LFO's display (the box with the
waveform in the LFO panel) and drop it on a knob. One LFO can drive as many
knobs as you like, each by its own depth.

- A modulated knob gets a coloured ring — teal for LFO 1, violet for LFO 2 —
  showing the range the LFO sweeps it through, and a white dot showing where
  the knob actually is right now.
- **Drag up or down on the ring** to change the depth. Dragging through zero
  inverts it. While you drag, the readout shows the depth.
- **Right-click** a knob to invert or remove an LFO, or remove all modulation.
- The LFO moves the knob around wherever you set it, by depth × the knob's
  full travel. Turning the knob still works and moves the centre of the
  sweep. Frequency knobs are swept evenly in octaves, as when you turn them.
- **Shape**: Sine, Triangle, Saw Up, Saw Down, Square, Sample & Hold, Smooth
  Random. **Sync** (on by default) locks the LFO to the host's bar position
  with a **Division** from 1/32 to 8 bars; off, **Rate** sets it in Hz. A
  synced random LFO plays the same pattern every time the song passes the
  same bars.
- Switches, menus and the LFOs' own settings cannot be modulated.

Assignments are saved with the session and in your own presets. Loading a
factory preset replaces them with the preset's own (most have none), and
adding or changing one marks the preset as edited.

### Echo delay

The **DELAY** section adds a feedback echo to the finished tail. Level at 0%
(the default) switches it off completely, so existing sessions and presets
are unchanged.

- **Sync** drives it just like the pre-delay: with Sync on, the delay's **Division** sets
  the echo time from the host tempo; with Sync off, **Time** does.
  Dotted 1/8 is the classic dub-techno choice — the repeats fall between the
  kicks and across the beat.
- The echo sits **before the ducker**, so every kick pumps the repeats down
  along with the tail. That keeps the echoes in the groove instead of
  smearing over the kick.
- Each repeat passes through **Tone** and **Low Cut** again, so it comes back
  darker than the last and never stacks up sub. Lower Tone for murkier,
  more distant repeats.
- A soft saturator in the feedback loop keeps it stable at any Feedback
  setting, and adds a little grit to long trails.
- Changing the echo time bends the pitch of the repeats already in flight
  for a moment, like a tape delay, rather than clicking.

### Dialling in a warehouse rumble

1. On a send or duplicate track, start with Decay ~4 s and Duck 90%.
2. Pull **Tone** down until only weight is left — usually 250–400 Hz. This is
   the single biggest "warehouse" control.
3. Raise **Damping** down to 600–1000 Hz so the tail darkens as it decays.
4. Set **Low Cut** to 30–45 Hz so the tail doesn't fight the kick's fundamental
   or eat all your headroom.
5. Set **Duck Threshold** near your kick's peak level, so each kick ducks the
   tail fully. The **Reduction** meter in the DUCK panel shows how far the
   tail is being pulled down; if it barely moves, lower the threshold.
6. Set **Duck Release** by ear against the tempo — the tail should reopen just
   before the next kick lands. This is what makes it groove rather than drone.
7. Add **Tail Drive** last, until the rumble is audible on a laptop speaker.
8. If it still disappears on small speakers, add **Enhance** rather than more
   Tail Drive — it targets the sub band specifically and leaves the weight
   on big systems untouched.

The **Filter** is the one to automate. Park Resonance around 4 and sweep the
cutoff across a breakdown; because it sits before the saturator, the drive
thickens whatever the filter leaves rather than fighting it.

Tips:
- If it sounds muddy, **raise Low Cut first** — it makes the bottom end decay
  faster without shortening the body of the tail. Lower Tone next. Only then
  reach for Decay.
- Longer Duck Release = more pronounced pumping; match it to your groove.
- Pitching the sampler's kick down before the reverb gives a deeper rumble.
- A short, punchy kick sample rumbles more cleanly than a long boomy one — the
  reverb supplies the length, so the source only needs to supply the hit.
- Feed the reverb from a *different* kick than the one you duck with: put the
  sampler's sub-kick into the sidechain input and a clickier layer into the
  main input.

### Why the tail keeps changing on its own

The eight delay lines are modulated by eight LFOs at 0.110, 0.153, 0.196 ...
0.411 Hz. The 0.043 Hz spacing means they beat against each other: adjacent
pairs realign every 23 seconds, the slowest cycles every 9 seconds, and the
bank as a whole never quite repeats. So the tail keeps drifting into new
configurations, and every so often several LFOs line up and the character
shifts noticeably. It is quasi-periodic by design, not random.

**Mod** sets how far it drifts, **Mod Rate** sets how often:

| Mod Rate | slowest LFO | shifts |
|---|---|---|
| 0.25x | 36 s | very slow, nearly static |
| 1x | 9 s | slow drift every 10–25 s |
| 2x | 4.5 s | noticeable movement |
| 4x | 2.3 s | obvious wobble |
| 8x | 1.1 s | chorus / vibrato |

To freeze it entirely, set Mod to 0. To keep the motion but make it rarer,
lower Mod Rate. To turn it into an effect, raise both.

One other thing used to change by itself: with the sidechain bus enabled but
nothing routed to it, the ducker switched trigger source depending on whether
signal was present. That is now the **Trigger** control. It defaults to
Input; Auto keeps the old switching behaviour if you want it.

### Filter slopes

The filter is a cascade of up to four state-variable stages: one for 12 dB/oct,
two for 24, four for 48. Cascading identical stages would multiply the
resonance and pull the corner frequency down, so the low and high pass modes
use Butterworth Q values across the cascade and the Resonance control scales
only the final stage — the one that produces the audible peak. At Resonance
0.707 the response is maximally flat by construction, and resonance then
behaves the same at every slope.

Notch and All Pass are single-stage; Deep Notch cascades two for a wider,
deeper cut. All Pass changes phase only and leaves the magnitude flat, which
is useful for lining the rumble up against the dry kick.

### Why the EQ bands sit so low

All three bands live in the bass, because that is all this plugin outputs.
With Tone at 320 Hz and Damping at 800 Hz, roughly 71% of the output energy
falls between 60 and 500 Hz and under 2% sits above 2.5 kHz. A conventional
high shelf up there would have nothing to work on — measured, a 2.5 kHz shelf
at +12 dB moved the output by 0.18 dB. Moved to 350 Hz, the same control
gives 3.5 dB.

The EQ shapes *within* the band Tone defines. If Edge still feels weak, raise
Tone first to give it something to work with.

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
- Parameters glide instead of stepping: filters, delay sizes and the like over
  about 10 ms, recomputed every 32 samples; Output, Width, Duck and the
  Dry/Wet controls per sample. Automation and LFOs do not zipper.

If you hear a tick surviving all of this, the source material genuinely
contains a bad sample and the plugin upstream is worth investigating.

## Layout

- `src/RumbleEngine.h` — the DSP, framework-agnostic and header-only
- `src/PluginProcessor.*` — JUCE wrapper, parameters, sidechain routing
- `src/PluginEditor.*` — knob panel
- `tools/offline_render.cpp` — offline WAV renderer
- `src/Lfo.h` — the LFOs and the modulation maths, framework-agnostic
- `tools/engine_tests.cpp` — DSP regression tests (see *Tests*)
- `tools/plugin_tests.cpp` — plugin-level tests: LFO assignments and switches, presets, state
- `assets/` — brand artwork; see `assets/README.md` for the logo spec

## How the engine works

1. Input is high-passed (at Low Cut) and low-passed so only the
   rumble-relevant band enters.
2. `tanh` drive with auto gain compensation.
3. Pre-delay. Each input sample is *scheduled* to come out after the current
   delay time, rather than read back from a moving tap, so a change of delay
   can never skip or repeat audio. That is what lets swing pick a delay per
   kick.
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
   EQ'd, sent through the echo delay (when Delay is above 0%), and
   multiplied by the ducker gain. The ducker reaches full depth when
   its trigger hits Duck Threshold.
8. Mid/side width with the side channel high-passed at Mono Below by a
   Linkwitz-Riley 24 dB/oct filter.

### Frequency-dependent decay, and what Decay actually means

Low Cut is a one-pole high pass **inside** the feedback loop, so low
frequencies lose a little energy on every pass and die away faster than the
mids. That is what keeps a rumble clean: with Decay at 4 s and Low Cut at
35 Hz, the 30-50 Hz band decays in about 1.7 s while the 250-500 Hz band
decays in about 4.0 s.

Without it, every band decays at the same rate, the sub sustains for the full
decay time, and successive kicks pile up into mud. If your rumble sounds
muddy, **raise Low Cut** -- it is the control that sets how fast the bottom
end clears out.

Because that filter also nibbles at the mid band, the feedback gain is given
a small makeup so the tail still tracks the knob. **Decay is specified at
400 Hz**, and the test suite checks the 400 Hz octave stays within 12% of the
setting from 1 s to 10 s. Bands below that decay faster by design, bands above
it faster still through Damping, and past Low Cut 250 Hz the makeup is capped,
since at that point you are deliberately gutting the low end and a shorter
tail is what you asked for.

(Low Cut also filters the network's *input*, so it shapes the tail twice: it
keeps sub out of the reverb in the first place, and makes whatever sub does
get in die away faster.)

Decay is also measured at low level. Tail Drive saturates the loud early part
of a tail more than the quiet end, which stretches the decay at realistic
levels. That is inherent to a saturating reverb: turn Tail Drive down and the
decay tightens back up.
