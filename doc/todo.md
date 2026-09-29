# Dede: to do

Improvements that are known and scoped but not required. Dede works without
any of them. Each item is marked with its priority and where the code lives.

The CPU item touches `fxme::SpectralBandSplitter`, which is in the FxmeTools
submodule (`lib/FxmeTools/core/FxmeTools/dsp/SpectralBandSplitter.h`), so it
is an edit to the shared library: keep its API unchanged, add a core test, and
note anything a consumer can observe in `lib/FxmeTools/doc/api-changes.md`.
Of the feature items (second half of this file), 16b lives in Dede's
`Source/` only. The others also change FxmeTools (the splitter,
`fxme::StereoCrossDelay`, `fxme::SpectrumRegionEditor`, or a new core class),
with the same rules as above.

The global controls callout (item 16, done, `GlobalPanel`, with the engine of
item 17, the FFT size of item 6 and the gate timing of item 7) is where 16b
(smoothing times) adds its controls.

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

CPU use was dominated by the FFT. `fxme::RealFft` moved onto WDL's real FFT on
2026-09-26 (about 10x faster), and on 2026-09-28 the splitter stopped doing
work that only produced zeros (items 2, 3 and 4, see `doc/done.md`; item 1 was
measured and dropped there too). What is left of the splitter's cost is
essentially one inverse FFT per region with something to output, about
0.1 % of a core each at 2048 points and 48 kHz.

## CPU

### 5. Spread the frame work across the hop

*Not mandatory. This does not lower the average load, only the peak.*

All the FFT work happens on one sample of every hop. At a host block of 128
samples, one block in four carries the whole cost while the other three do
almost nothing, and a DAW's CPU meter (and the risk of a dropout) follows the
worst block, not the average. Staggering the regions (rendering region `b` at
its own offset within the hop, with the latency adjusted to match) would even
this out. More invasive than the items above; only worth it if dropouts show
up at small buffer sizes while the average load looks fine.

### 18. SIMD across the live engine's bands

*Not mandatory. Expected gain: the live engine's filters several times
cheaper.*

The live engine (item 17, done) costs about as much as the spectral one,
about 1 % of a core for 8 regions, and its filters are most of it: four
biquads per band, each run over the block one after the other, so every
sample waits on the previous one's result. The 8 bands are independent and
run the same structure, so they fit SIMD lanes: one 8-wide (AVX) or two
4-wide (SSE, NEON) transposed-direct-form biquads per stage, the four stages
in one pass per sample. Needs a band-parallel filter in FxmeTools core (the
coefficients and states as lane arrays, with a scalar fallback), fed by
`FilterBankSplitter`, whose outputs stay one buffer per band. Check it
against `EdgeBandPass` alone, sample for sample, as the current test does.

## Features

Background. The spectrum view measures exactly as the gates do: the same
window size (the `fftSize` parameter, item 6, done), the same window and dB
scale, the peak detector and no temporal averaging, all locked (see
`architecture.md`, "The view measures exactly as the gates do"). The gate
itself compares raw per-bin magnitudes every hop (about 11 ms) and smooths
only the gain it applies, with the attack and release of item 7 (done).

### 8. Gate detector smoothing, matched to the view (later)

*Not mandatory. Only if the gate still warbles on noisy material with a
longer release (item 7, done).*

Smooth each bin's level before the threshold comparison (an exponential
average of `magSq` across hops, time constant as a parameter), so the gate
reacts to a sustained level rather than to every frame. Then set the view's
averaging from the same parameter (N about τ / 40 ms, its 25 Hz frame), so the
plotted trace is the level the gate compares. Those badges are locked since
item 6 (`setMeasurementLocked`): unlock, set the view state, lock again. This is an
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

### 16b. Smoothing times in the global controls

*Not mandatory. What item 16 (done) left out.*

The glide times that are hard-coded today, one parameter each in a
"Smoothing" section of `GlobalPanel`, with today's values as defaults so
nothing changes:

- region gain and pan glide, 20 ms (`SpectralBandSplitter::setLevelSmoothingSeconds`,
  and the pan smoothers in the processor);
- region output levels (Dry, Delay, Send) glide, 20 ms (the processor's
  smoothers);
- border glide, 20 ms (`fxme::EdgeBandPass`'s smoothing: keep in band, and
  the live engine's band filters);
- solo and free fade, 5 ms (`audibleGain`);
- delay-time glide, if `fxme::StereoCrossDelay`'s smoothed time exposes its
  time constant (check `DelayLine`; an FxmeTools change otherwise).

Keep the list short: a smoothing nobody will ever want to change does not
earn a control. Re-save the factory presets.

## Verifying a change

- Core: `cmake -S lib/FxmeTools/core -B build-core && cmake --build build-core -j2 && ctest --test-dir build-core --output-on-failure`.
- The splitter's levels must keep matching `fxme::SpectrumAnalyzer`'s (see the
  note in `lib/FxmeTools/doc/api-changes.md`), and each region's output must
  be unchanged sample for sample (within float rounding). A core test that
  records the current outputs for a few region layouts, written before
  starting on items 1 to 3, makes that easy to check.
- Then build the plugin, which is the real check.
