# Dede architecture

Dede is an FX-Mechanics JUCE audio effect: a spectral multi-delay with a shared
reverb. The user draws rectangular regions on a spectrum view; each region is a
frequency band with its own spectral gate, gain and pan, feeding its own stereo
delay and a send to one common reverb.

This document tracks the plugin as it is built. It is kept in sync with the code
step by step, and is the first thing to read when picking the project back up.

## Status

All five steps are in place. Dede is feature complete against its original
spec: regions drawn on the spectrum, each with a gate, a gain, a pan, its own
stereo delay in three time modes, and a send to a shared reverb.

| Step | Content | State |
|---|---|---|
| 1 | Skeleton, parameters, preset manager, editor shell, spectrum view | done |
| 2 | `fxme::SpectralBandSplitter` and per-region gate, gain, pan | done |
| 3 | `fxme::SpectrumRegionEditor` (drawing and editing regions) | done |
| 4 | `fxme::StereoCrossDelay`, delay-time modes, region popup | done |
| 5 | `fxme::Reverb` over WDL, sends, factory presets, CI | done |

## Signal flow

```
                        ┌──────────────── dry ────────────────┐
                        │                                     │
  in ──> spectrum tap ──┴─> band splitter ──> region 0 ──> degrade ──> pan ─┬─> region Dry ───┬─> out
                              (STFT)      │                                 └─> delay ─> Delay ┤
                                          ├─> region 1 ── ... (same strip)                     │
                                          └─> ...                                              │
                                              per-region sends (from Dry + Delay) ──> reverb ──┘
```

A region is a small channel strip. Its band, degraded and panned, leaves it two
ways, each at its own level: as it is, undelayed (the region's **Dry**, off by
default), and through its delay (**Delay**, 0 dB by default). The reverb send
is taken from the sum of the two, after their levels, like a post-fader send:
with Delay at -inf the reverb gets no repeats from that region, and with both
down it gets nothing from it.

The region's Dry is *added* to the global dry, which stays the untouched
input: it does not replace that frequency range in it. To hear only the
processed band, turn the global Dry down (which also removes everything outside
the regions). The regions' Dry outputs are on a bus of their own, scaled only
by the output trim, not by the global Dry nor by Delays.

Each region leaves the splitter mono, with its gain applied but not its pan
(`setApplyPan (false)`). It goes through its degrade chain (saturation, then
bit-crush, then downsample, each with its own switch and skipped when off),
and only then is panned, constant power, before its delay. Panning after the
degrade chain keeps a saturator from driving the louder side of a panned
region harder. The drive knob sets how hard the band clips, not how loud it
comes out: the saturator's output is divided by its drive (unity for small
signals, instantly), then an `fxme::LevelMatcher` brings it back to the
band's RMS before the saturator (200 ms followers). The division alone is
not enough: a band that clips is capped whatever the drive, so it would get
quieter as the drive rises (26 dB quieter at 36 dB of drive on a loud tone).
The saturator's bias and sag are fixed per model (sag 0.5 except Standard,
Class AB with a -0.1 overlap for its crossover notch).

The degrade chain spreads energy outside the region (harmonics from the
saturator, aliasing from bit-crush and downsample). Its **Keep in band**
switch, last on the Degrade tab, band-passes the region back to its range
right after the chain (`fxme::EdgeBandPass`: 24 dB per octave on each side,
edges smoothed so dragging a border does not zipper). With the region's pitch
shift on, it follows the *shifted* range, or it would cancel the shift. The
slopes are much softer than the splitter's spectral mask, so a little of the
spill remains near the edges, and a region narrower than about an octave loses
some level at its centre (about 2 dB for half an octave).

The reverb runs on every block whatever the sends are doing, so a tail keeps
ringing after the region that fed it has been turned down or deleted.

Three switches per region, in the region column left of the spectrum, act at
different points and are worth keeping apart:

- **On** (`act`) is at the input: off, the splitter band is disabled (which
  also saves its inverse FFT), so nothing new enters the region, but its delay
  runs on and its tails ring out. The region stays in place and editable.
- **Solo** (`solo`) is at the output: when any region is soloed, the others
  are silenced by a ramped gain (5 ms) on their Dry and Delay, and so on their
  reverb sends, which are taken from them. Their processing goes on, so
  releasing the solo comes back into running tails. The global dry is not
  affected. (There was a per-region Mute on the same gain; it was removed as
  redundant with On. The difference: On lets the tails ring out, a mute cut
  them at once.)
- **Free** (the `on` parameter, "slot in use") deletes the region. Its output
  fades through the same ramp, then the slot stops being processed. When the
  slot is used again, every piece of its state (delay lines, degrade and
  keep-in-band filters, spectral effects, meters, smoothed levels) is cleared
  first, so a new region never starts by replaying its predecessor's tail.

A region that is off, or silenced by another's solo, is drawn dimmed on the
spectrum. Each row also shows two meters: how much of the region's energy its
gate lets through (`SpectralBandSplitter::getGateOpenness`), and the region's
output level, left and right, measured before the solo gain (one
`fxme::VuMeter` per channel per region, 100 ms RMS).

The global dry signal is never processed: it is the untouched input,
level-trimmed. Everything the regions do is added on top, so with no region
enabled Dede is a gain stage.

## The spectral stage

Regions are not bandpass filters. The splitter runs one short-time Fourier
transform over the input (`fxme::SpectralBandSplitter`, periodic Hann, 75 %
overlap-add, one analysis FFT per hop and one inverse FFT per active region
that has something to output),
and a region is a set of bins plus a per-bin gate: inside its interval, bins
quieter than the gate threshold are muted and the rest pass. That is what lets
a region take only the peaks of its band, or only its noise floor, which no
filter can do. A per-bin ceiling mirrors the gate (bins louder than it are
muted), so a region can also keep only the level window between the two
lines, for instance the quiet tails and room of a sound without its loud
notes. A global knee turns both switches into smooth transitions a given
number of dB wide.

Each region can also be transformed inside the frame, before it leaves the
splitter: **pitch shift** (fractional semitones), **freeze** (tonal, or a
random-phase wash) and **blur** (magnitudes smoothed over time), by
`fxme::SpectralBandEffects`, installed as the splitter's band processor. They
share one phase vocoder (frequency tracking once per hop, for every region),
cost no extra transform and add no latency. Order per region: gate and
ceiling, then freeze or blur, then pitch. A region with all three off is left
untouched bit for bit. The spectral transforms come *before* the degrade
chain, which works on the region's time-domain output.

Three consequences shape the rest of the design:

- Everything spectral happens on the mono sum of the input. The spectrum view
  shows that same mono sum, so a gate line drawn on the trace is compared
  against exactly the signal it is drawn over. It is also why a region has a
  pan rather than a balance: the band it emits is mono until it is panned.
- Gain and pan are applied to the region's time-domain output through smoothed
  values, not to its bins. Moving them is click-free and costs no transform.
- The splitter delays by exactly one window: the `fftSize` parameter, 1024 to
  16384 points, default 2048 (43 ms at 48 kHz; 21 ms at 1024, 341 ms at
  16384). The dry path is delayed by the same amount with a
  `juce::dsp::DelayLine`, and the total is reported with `setLatencySamples`,
  so the host puts everything back in place. Larger windows give narrower
  regions and smear transients more; the noise floor per bin also drops about
  3 dB per doubling, so the same gate setting gates noise differently at
  different sizes.

Changing the size while running re-allocates, so it never happens on the audio
thread: a parameter listener schedules an async update, and on the message
thread `handleAsyncUpdate()` suspends processing (which waits for any block in
flight), re-prepares the splitter, the spectral effects and the dry delay at
the new size (`configureSpectralEngine`), reports the new latency, and resumes.
The host gets about one window of silence, and some hosts only pick up the new
latency on transport stop. The parameter is not automatable.

**The view measures exactly as the gates do.** The gate threshold is a
per-bin level in the dB convention of `fxme::SpectrumAnalyzer`, and the editor
locks every way the view could measure differently:

- the same window size (followed when `fftSize` changes);
- the same window (periodic Hann) and dB scale: FxmeTools' tests pin that the
  analyser reads a tone at the splitter's level to 0.001 dB, at every size;
- the peak detector: where many bins share one point (the highs), the point
  shows the loudest, so the trace is over a gate line exactly when some bin
  there passes it. The average detector would draw a passing tone under the
  line;
- no temporal averaging: the gate reacts to each frame as it is, so the trace
  does too.

The spectrum display's badges for these are locked (drawn dimmed). If the
trace is too jumpy to read, the honest cure is to smooth the gate's own
detector and the view with it (todo item 8), not the picture alone.

## Two engines: Spectral and Live

The `engine` parameter (global controls, not automatable) picks what cuts the
regions out of the input. **Spectral** (the default) is the splitter above.
**Live** is `fxme::FilterBankSplitter`: per region an `fxme::EdgeBandPass`
(4th-order Butterworth each side, 24 dB per octave) on the mono input, then an
`fxme::BandGate` on the band's level, then its gain. Everything after the band
(degrade, keep in band, pan, Dry, delay, send, solo, meters) is shared and
does not know which engine fed it.

| | Spectral (default) | Live |
|---|---|---|
| Latency | one FFT window (43 ms at 2048) | none |
| CPU, 8 regions | about 1 % of a core at 2048 points | about the same (the filters are most of it) |
| Gate and ceiling | per frequency bin: keep only the peaks of a band, or only its floor | per band: the whole band opens or closes (a multiband gate), per sample |
| Region edges | near brick-wall | 24 dB per octave: neighbours overlap, narrow regions lose level (about 2 dB at half an octave) |
| Transients | softened by the window (pre-echo) | intact |
| Spectral tab (pitch, freeze, blur) | yes | no effect (the tab is dimmed and says so) |
| Region Dry added to the global dry | phase-aligned, clean | phase shifts near the edges colour the sum there a little |

**Levels.** A band gate compares the band's level in the same convention as
the spectrum trace for a steady tone (a sine of amplitude A reads A/2, what
its centre bin reads through the Hann window), so a region's gate and ceiling
mean the same thing in both engines for tonal material. For anything
noise-like, a band's level sums all its bins and sits above the trace. So in
Live the view shows each region's band level (`getBandLevelDb`) as a shaded
column topped by a solid line (`SpectrumRegionEditor::setLevelMarkersEnabled`),
and the gate and ceiling are set against that. The detector averages the
squared band over about one period of its lowest frequency (3 to 50 ms), so a
low band does not ripple through its gate.

**Switching** takes the path of the FFT size: the parameter listener schedules
`handleAsyncUpdate()`, which suspends processing, calls `configureEngine()`
(prepares the engine switched to, sets the dry delay to its latency, reports
it with `setLatencySamples`) and resets every region, delay tails included,
since they were lined up with the other engine's timing. Only the active
engine is prepared, fed and read. An FFT size change while in Live is only
recorded; it is applied when switching back. The Region Dry colouring near the
edges in Live has no cheap cure: an all-pass on the global dry cannot match
eight overlapping arbitrary bands.

## Where code lives

Anything not specific to Dede goes into the `lib/FxmeTools` submodule under
`fxme::`, and Dede keeps only its own glue. The split for this plugin:

FxmeTools (new or extended, see the steps above):

- `dsp/SpectralBandSplitter.h` — the STFT band split with per-band spectral
  gate, gain and pan. Added in step 2, and part of the module umbrella header;
  now in FxmeTools core, on `fxme::RealFft` (WDL's real FFT). Later: a
  ceiling and a knee, an unpanned output option, and a per-band processor
  hook (`SpectralBandProcessor`).
- `dsp/SpectralBandEffects.h` — pitch, freeze and blur as such a processor,
  on a shared phase vocoder. Core, header-only, in the module umbrella.
- `components/SpectrumDisplay.h` — extended in step 3, backward compatibly:
  `getPlotArea`, `freqToX`, `xToFreq`, `dbToY`, `yToDb`, `overBadge` and
  `overLegend` moved from private to protected, two paint hooks
  (`paintBehindTraces`, `paintOverTraces`) were added, and the analysis window
  can now be set and pinned from code (`setFftOrder`, `setFftSizeLocked`).
  Nothing that existed changed meaning.
- `components/SpectrumRegionEditor.h/.cpp` — added in step 3: a fixed pool of
  rectangular regions over a `SpectrumDisplay`, with border, gate and gain/pan
  handles, drag-to-create and Delete-to-remove. Later: the handle under the
  pointer is highlighted, and an opt-in ceiling line (`setCeilingEnabled`)
  mirrors the gate; the two push each other apart when dragged, and the hit
  test picks the nearest line (between gate and ceiling, by their midpoint). It owns no values: every
  gesture reports through a callback and the consumer pushes the result back
  with `setRegion`, which is what keeps the APVTS the single source of truth.
- `dsp/StereoCrossDelay.h` — added in step 4: two `fxme::DelayLine`s (their own
  feedback switched off) under a left, right and cross feedback matrix, with
  damping on the fed-back signal. It scales the three gains together whenever
  either line would receive a total of 1 or more, so no combination of
  parameter values can turn it into an oscillator.
- `dsp/DelayTimeResolver.h` — added in step 4: one delay value read as seconds,
  a proportion of a whole note, or a multiple of a note period.
- `dsp/Reverb.h` — added in step 5: a float, block-oriented wrapper over
  `WDL_ReverbEngine` (WDL/verbengine.h, Freeverb). It owns the double
  conversion buffers, maps a 0 to 1 room control onto the range the engine is
  actually useful over, and calls the engine's `Reset()` only when room size or
  damping have really moved (they reach the combs no other way, and it re-tunes
  every one of them). Not in the module umbrella, since it needs the WDL
  headers: include `<FxmeTools/dsp/Reverb.h>` directly.

Dede's own `Source/`:

- `ParamIDs.h` — every APVTS identifier and the shared parameter limits.
- `Theme.h` — palette, per-region colours, control styling, geometry.
- `PluginProcessor.{h,cpp}` — APVTS, cached parameter pointers, transport and
  MIDI state, the audio path.
- `PluginEditor.{h,cpp}` — window layout, global controls, preset chrome, and
  the wiring between the region editor and the parameters.
- `RegionPanel.{h,cpp}` — the callout opened by clicking a region: its two
  delay times and their mode, its three feedback gains and its reverb send.
  Nothing that has a handle on the spectrum appears here.

## The editor and the parameters

Layout. A top bar (preset strip, preset browser, the gear that opens the
global controls, help); the spectrum view; and on its left a column holding
one row per region slot (`RegionColumn`), the four levels in a 2 x 2 grid
(Dry, Delays, Reverb return, Output), and the output meter (`OutputMeter`,
RMS over 100 ms plus a peak marker, from `getOutputLevelDb` and
`takeOutputPeakDb`). What is set once rather than played is in the global
controls callout (`GlobalPanel`): the reverb's room, damping and width, the
FFT size, and the gate's attack, release and knee.

The region editor never holds a value of its own. Every gesture calls back into
`PluginEditor`, which writes to the APVTS through
`setValueNotifyingHost`, and a 20 Hz timer reads the parameters back and pushes
them into the view. Automation, a preset load and a drag all take the same
route, so none of them needs special handling and none of them can leave the
view showing something the DSP is not doing. Drags are bracketed in
`beginChangeGesture`/`endChangeGesture` on exactly the parameters that handle
touches, so a host records one gesture rather than a stream of writes.

Creating a region means finding the first slot whose `on` parameter is false
and filling it in; deleting one means setting that parameter back to false. The
slot keeps its index, its colour and its position in the pool either way.

## Parameters

An APVTS parameter set cannot grow at runtime and stay automatable, so Dede has
a fixed pool of `dede::maxRegions` (8) region slots. A slot the user has not
created has its `on` parameter false and costs nothing on the audio thread.
Region identifiers are built as `rg<slot>_<suffix>` (`rg0_flo`, `rg3_fbx`); the
scheme is part of the saved state, so slots are never renumbered and suffixes
never renamed.

Global: `dryLevel`, `wetLevel` (the summed delays), `outLevel`, the shared
reverb's `revRoom`, `revDamp`, `revWidth`, `revLevel` (the first three in the
global controls callout, `GlobalPanel`, opened from the gear in the top bar).
Also in that callout: `fftSize` (choice, 1024 to 16384
points, not automatable), `gateKnee` (dB, the width of the transition around
every gate and ceiling line; 0 is a hard switch), `gateAtk` and `gateRel` (ms,
the splitter's per-bin attack and release, applied once per hop). Presets
recall them like any other parameter, the FFT size (and so the latency)
included.

Per region: `on` (slot in use), `act` (fed by its band, default on), `solo`
(not automatable; saved in presets like the rest, so a preset recalls
the sound as it was heard), `flo`, `fhi` (band borders, logarithmic 20 Hz to
20 kHz),
`gate` (dB, at its minimum the region is fully open), `ceil` (dB, same range
as the gate, at its maximum it is off), `gain` (dB), `pan`,
`timel`, `timer` (0.001 to 3, meaning set by the mode), `mode` (seconds, DAW
sync, MIDI note), `fbl`, `fbr`, `fbx` (dB, minimum reads as silence), `dry`
(dB, the region's own band undelayed into the mix, default -inf), `dly` (dB,
its repeats into the mix, default 0 dB) and `send` (dB, to the reverb, taken
from the region's output after `dry` and `dly`).

Degrade, per region: `saton`, `satmod` (Standard, Dynamic, Triode, Class AB),
`satdrv` (0 to 48 dB, default 12), `crshon`, `bits` (1 to 24, fractional,
default 8), `dson`, `dsf` (hold length 1 to 1024, logarithmic, default 4),
`keep` (keep in band). All default to off.

Spectral, per region: `pshon`, `pshst` (-24 to +24 semitones, 0.01 steps),
`frzon` (latched: captures the region's spectrum when switched on), `frzmod`
(Tonal, Wash), `blron`, `blrms` (20 ms to 5 s, logarithmic, default 300 ms).
All off by default.

A slot's default band is the audible range cut into eight equal frequency
ratios, so a freshly enabled slot is never degenerate.

## Delay-time modes

One value per side, 0.001 to 3, read three ways:

- Seconds: the value is the delay in seconds.
- DAW sync: the value is a proportion of a whole note, so 0.25 is a quarter
  note. The tempo comes from the host playhead, falling back to 120 BPM.
- MIDI note: the value multiplies the period of the most recent MIDI note in.
  This is why the plugin declares MIDI input while being an audio effect.

Sync and MIDI-note modes can ask for far more than 3 seconds, so the delay
buffers are sized for `dede::maxDelaySeconds` (6 s, three whole notes at 120
BPM) and `fxme::DelayTimeResolver` clamps to that. Six is a compromise rather
than a limit of the design: there are two delay lines per region slot, so every
extra second costs real memory whether or not anyone uses it.

Feedback is a dB value whose minimum means silence rather than -60 dB, so a
delay set to no feedback is exactly one repeat. `fxme::StereoCrossDelay` then
scales the three gains together if either line would receive a total of 1 or
more, which is why no combination of parameter values can make it oscillate.

## State

`getStateInformation` writes the APVTS state XML with a `version` attribute
from the first release, so later formats can be migrated in
`setStateInformation`. The preset manager stores the current preset name and
dirty flag on the state tree itself, so both round-trip through host sessions
and preset files.

Factory presets are embedded from `Source/Assets/*.xml` via
`juce_add_binary_data`. The CMake sets `DEDE_HAS_BINARY_DATA` from whether that
folder has anything in it, so the processor falls back to a user-presets-only
manager if the assets are ever removed.

The preset files are generated by `tools/make_presets.py` rather than written
by hand. Each one is a complete APVTS state dump, all 111 parameters: a preset
that omitted any of them would leave whatever the previously loaded preset had
set, because `replaceState` swaps the whole tree. The script's defaults have to
stay in step with `createParameterLayout()`.

The editor carries both preset UIs: `fxme::PresetBarComponent` sits in the top
bar and walks the list, and the button beside it opens the full
`fxme::PresetComponent` in a callout for saving, renaming and deleting.

## Build

JUCE is expected as a sibling directory (`../JUCE`). FxmeTools is a submodule
under `lib/`, and carries WDL as its own nested submodule, so the checkout has
to be recursive:

```sh
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2 --target Dede_VST3
```

None of the WDL sources are compiled: the band splitter runs on
`juce::dsp::FFT`, and the reverb engine Dede takes from WDL
(`WDL/verbengine.h`) is header-only. The nested submodule still has to be
checked out for that header to exist.

Builds do not install: the built `.vst3` has to be copied into the VST3 folder
and the DAW made to rescan.

`.github/workflows/release.yml` builds Linux, Windows and a macOS universal
binary on a version tag, and can be run by hand from the Actions tab without
publishing. Two things in it are load-bearing: the macOS architectures and
deployment target are passed as `-D` on the configure line as well as being set
before `project()` in CMakeLists, and the verification step exits non-zero when
a slice is missing rather than only printing `lipo -info`.
