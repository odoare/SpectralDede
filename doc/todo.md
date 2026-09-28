# Dede: to do

Improvements that are known and scoped but not required. Dede works without
any of them. Each item is marked with its priority and where the code lives.

The CPU items touch `fxme::SpectralBandSplitter`, which is in the FxmeTools
submodule (`lib/FxmeTools/core/FxmeTools/dsp/SpectralBandSplitter.h`), so they
are edits to the shared library: keep its API unchanged, add a core test, and
note anything a consumer can observe in `lib/FxmeTools/doc/api-changes.md`.
Of the feature items (second half of this file), 6, 7 and 11 live in Dede's
`Source/` only. The others also change FxmeTools (the splitter,
`fxme::StereoCrossDelay`, `fxme::SpectrumRegionEditor`, or a new core class),
with the same rules as above.

**Parameters and saved state.** Items 6, 7, 9, 12, 13, 14 and 15 add
parameters. Dede has not been released, so there is no state migration to
write: the parameter layout will be tagged as version 1 at the first release,
and migrations start after that. One thing still applies before release:
`AudioProcessorValueTreeState::replaceState` (used by `setStateInformation`
and by the preset manager) leaves a parameter that is missing from the loaded
tree at its *current* value, not its default. A factory preset saved before a
parameter existed therefore inherits whatever the user last set. Re-save the
factory presets whenever parameters are added.

Done items are moved to `doc/done.md`, keeping their numbers.

## Context

CPU use was dominated by the FFT. `fxme::RealFft` used to run a scalar complex
FFT on real input; on 2026-09-26 it was moved onto WDL's real FFT (about 10x
faster for a forward + inverse pair at 2048 points). The items below are what
is left after that change, in order of expected gain.

The splitter's cost per hop (512 samples at the default 2048-point window,
75% overlap) is one forward FFT plus one inverse FFT per enabled region, so up
to 9 transforms with `maxRegions = 8`, plus a few full-spectrum passes per
region.

## CPU

### 1. Two regions per inverse FFT

*Not mandatory. Expected gain: inverse transforms roughly halved, which is most
of the splitter's remaining cost when many regions are enabled.*

Each region's masked spectrum is the spectrum of a real signal, so two of them
(A and B) can share one complex inverse FFT: transform `A + jB`, and the real
part of the result is region A's frame, the imaginary part region B's.

Prerequisite: a fast complex inverse. `fxme::Fft` (complex) is still the scalar
implementation. It would need the same treatment `RealFft` got (a
`WDL_fft` backend for sizes up to 32768, with the permutation undone), or the
splitter could call a new packed-pair entry point on `RealFft` instead.

### 2. Only touch the bins inside each region

*Not mandatory. Expected gain: moderate, grows with the number of narrow
regions.*

In `renderFrame()`, every enabled region copies the whole spectrum into `frame`
and then runs the mask and gate over all `fftSize / 2 + 1` bins, although only
bins `kLo..kHi` can be non-zero. Instead: zero `frame`, then fill only
`kLo..kHi` with the masked, gated bins.

Care point: bins outside the band currently get their `gateGain` reset to 0 on
every hop. If the loop skips them, a region whose edges move would reopen with
stale gate states on the newly covered bins. Reset the gains of the bins that
enter the band when `setBand()` changes `lowHz` / `highHz` (or keep
the reset loop, which is cheap compared with the mask).

### 3. Skip the inverse FFT when a region's frame is silent

*Not mandatory. Expected gain: large in the specific case of gated or empty
regions, nothing otherwise.*

When every gain in `kLo..kHi` is 0 (gate fully closed, or the band is narrower
than one bin), the inverse transform and the overlap-add only add zeros. Track
whether any bin gain was non-zero during the mask loop and skip both steps if
none was.

### 4. Remove the modulo from the history copy

*Not mandatory. Expected gain: small.*

`renderFrame()` reads the history ring with `(histPos + i) % fftSize` for every
sample of the window. Replace it with two straight loops (from `histPos` to
the end, then from 0), or keep a mirrored double-length history so the window
is one contiguous read.

### 5. Spread the frame work across the hop

*Not mandatory. This does not lower the average load, only the peak.*

All the FFT work happens on one sample of every hop. At a host block of 128
samples, one block in four carries the whole cost while the other three do
almost nothing, and a DAW's CPU meter (and the risk of a dropout) follows the
worst block, not the average. Staggering the regions (rendering region `b` at
its own offset within the hop, with the latency adjusted to match) would even
this out. More invasive than the items above; only worth it if dropouts show
up at small buffer sizes while the average load looks fine.

## Features

Background. The spectrum view's FFT-size badge is locked on purpose: the gate
thresholds are per-bin levels measured in the splitter's own window
(`dede::splitterFftOrder`, 2048 points), and the view is pinned to that window
so a gate line sits at the level the DSP compares against
(`Source/PluginEditor.cpp`, `setFftOrder` / `setFftSizeLocked`). The view's
other controls do not map onto the gate: the avg/peak badge chooses how several
bins merge into one plotted point (frequency, not time), and the avg / N badges
smooth the picture at the GUI's 25 Hz refresh, whereas the gate compares raw
per-bin magnitudes every hop (about 11 ms) and smooths only the gain it
applies. Hence the two items below: make the window size a real parameter that
the DSP and the view both follow, and expose the gate timing that actually
exists.

### 6. FFT size as a parameter

*Not mandatory. Planned.*

1. **Parameter.** Add `ids::fftSize` in `Source/ParamIDs.h`: an
   `AudioParameterChoice` "Resolution" over 1024 / 2048 / 4096 / 8192 / 16384
   (orders 10 to 14, the overlap of what the splitter, 8 to 14, and the
   spectrum view, 10 to 15, accept), default 2048 so existing sessions sound
   the same. Mark it non-automatable
   (`AudioParameterChoiceAttributes().withAutomatable (false)`): changing it
   re-allocates and changes the latency, which must not happen from an
   automation lane. Replace the `splitterFftOrder` constant by a helper that
   turns the choice index into an order, and keep `splitterFftOrder` as the
   default.
2. **prepareToPlay.** Read the parameter instead of the constant in
   `splitter.prepare (...)`. The dry delay and `setLatencySamples()` already
   follow `splitter.getLatencySamples()`, so they need no change there.
3. **Changing it while running.** `splitter.prepare()` allocates, so it must
   not run on the audio thread. Have the processor listen to the parameter
   (`apvts.addParameterListener`) and, since the callback can come from any
   thread, only `triggerAsyncUpdate()` there (make the processor a
   `juce::AsyncUpdater`). In `handleAsyncUpdate()`, on the message thread:
   `suspendProcessing (true)` (it takes the callback lock, so no block is in
   flight), re-prepare the splitter at the new order with the current sample
   rate and block size, then `spectral.prepare()` from the splitter's new FFT
   and hop sizes (the pitch, freeze and blur state is sized from them, see
   item 13 in `done.md`), re-prepare and re-delay `dryDelay`, call
   `setLatencySamples()`, then `suspendProcessing (false)`. Skip it if the
   order did not actually change, and if `prepareToPlay` has not run yet. The
   band settings survive because `updateBands()` re-sends every band each block
   (the gate threshold depends on the window size, so this matters).
4. **Editor.** Add a combo box for the parameter (FxmeTools look-and-feel,
   `setLookAndFeel()` on it, attached with a `ComboBoxAttachment`), near the
   spectrum view. In the editor timer, when the parameter's order differs from
   `spectrum.getFftOrder()`, call `spectrum.setFftOrder()`. Keep
   `setFftSizeLocked (true)`: the badge becomes a read-out and the combo is the
   control. (Making the badge itself the control would need an
   `onFftOrderChanged` callback added to `fxme::SpectrumDisplay`.)
5. **Presets.** Re-save the factory presets so they carry the new parameter
   (see the note at the top), and check they load at 2048.
6. **Docs.** Update `doc/architecture.md` (the latency paragraph that names
   `splitterFftOrder`) and the info-button text.

Things to know when testing:

- Latency is one window: 21 ms at 1024, 43 ms at 2048, 170 ms at 8192, 341 ms
  at 16384 (48 kHz). Most hosts accept a latency change while running; some
  only pick it up on transport stop or re-activation. Try Reaper and Bitwig at
  least.
- Larger windows give narrower bands but smear transients (pre-echo before
  attacks); smaller ones the opposite. CPU per sample is roughly constant.
- A sine keeps its level at every size (the level convention is
  `mag * 2 / fftSize`), but the noise floor per bin drops about 3 dB per
  doubling. The same gate setting therefore gates noise differently at
  different sizes. The view follows the size, so what is drawn stays right.
- There is an audible gap of one window when the size changes. Acceptable for a
  setting, but worth a sentence in the info text.

### 7. Gate attack and release as parameters

*Not mandatory. Planned.*

The splitter already has `setGateTimes (attackSeconds, releaseSeconds)`
(defaults 5 ms and 80 ms): the per-bin gate gain moves towards open or closed
with these time constants, one step per hop. It is realtime-safe (it only
recomputes two coefficients), so this needs no FxmeTools change.

1. **Parameters.** Two global `AudioParameterFloat`s, `ids::gateAttack`
   ("Gate attack", about 1 to 100 ms, skewed, default 5 ms) and
   `ids::gateRelease` ("Gate release", about 10 to 1000 ms, skewed, default
   80 ms). Global rather than per region, to keep the region popup small; per
   region is possible later but would need per-band times in the splitter.
2. **DSP.** In `processBlock`, next to `updateBands()`, call
   `splitter.setGateTimes()` with the two values, converted to seconds. Cheap
   enough to do every block; or only when either value changed.
3. **Editor.** Two knobs (`fxme::FxmeSlider`, see the `fxme-juce-controls`
   skill) next to the Knee knob in the bottom row (item 9 put it just left of
   the reverb group, as the start of a gate section). Show milliseconds.
4. **Presets.** The defaults equal the current hard-coded times, so nothing
   changes by default. Re-save the factory presets (see the note at the top).
5. **Note.** The time constants are applied per hop, so their resolution is
   one hop (about 11 ms at 2048 points, more at larger sizes). An attack below
   one hop is effectively instant. Worth saying in the info text, and a reason
   to keep the attack range's low end at a few ms.

### 8. Gate detector smoothing, matched to the view (later)

*Not mandatory. Only if the gate still warbles on noisy material after item 7.*

Smooth each bin's level before the threshold comparison (an exponential
average of `magSq` across hops, time constant as a parameter), so the gate
reacts to a sustained level rather than to every frame. Then set the view's
averaging from the same parameter (N about τ / 40 ms, its 25 Hz frame) and lock
those badges, so the plotted trace is the level the gate compares. This is an
FxmeTools change (`SpectralBandSplitter`: a per-band smoothed-level array and a
setter), so it needs a core test and an `api-changes.md` entry. Costs one
multiply-add per bin per region per hop, and it delays the gate's opening on
attacks by roughly the time constant.

### 13b. Shimmer (pitch shift inside the delay feedback)

*Optional. Items 12 and 13, its prerequisites, are done.*

The classic shimmer: each repeat comes back shifted again (typically an
octave up), so the repeats climb. This cannot be the spectral shifter of item
13, because the feedback loop runs in the time domain after the splitter. It
is the job of `fxme::PitchShifter` (time-domain, two read taps sweeping a
short delay line with a crossfade): any ratio, per sample, can sit in a loop.
It was designed for small detunes, so large intervals sound somewhat grainy;
use a longer window (80 to 100 ms) for octaves, and judge by ear.

1. **Feedback hook in `fxme::StereoCrossDelay`** (FxmeTools): an optional
   insert on the fed-back signal (an interface pointer the consumer provides).
   The same hook serves degrade-in-the-loop (item 12) and keep-in-band in the
   loop (item 14, done: another `fxme::EdgeBandPass` per channel inside the
   loop, on the same edges). Core test: no insert gives bit-identical output.
2. **Parameters (per region):** shimmer on, interval (semitones, float,
   default +12), amount (how much of the shifted signal goes into the
   feedback versus the unshifted one). In the Delay tab.
3. A gentle low-pass in the loop (fixed, around 10 kHz) keeps octave-up
   feedback from piling up as a whistle. The feedback gains are already at
   most 0 dB, so the loop stays stable.
4. `fxme::PitchShifter` smooths its ratio internally, so the interval can be
   moved without zipper noise.

## Verifying a change

- Core: `cmake -S lib/FxmeTools/core -B build-core && cmake --build build-core -j2 && ctest --test-dir build-core --output-on-failure`.
- The splitter's levels must keep matching `fxme::SpectrumAnalyzer`'s (see the
  note in `lib/FxmeTools/doc/api-changes.md`), and each region's output must
  be unchanged sample for sample (within float rounding). A core test that
  records the current outputs for a few region layouts, written before
  starting on items 1 to 3, makes that easy to check.
- Then build the plugin, which is the real check.
