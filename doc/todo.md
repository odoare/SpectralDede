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
with the same rules as above. Items 6, 7, 9, 12, 13, 14 and 15 all add
parameters: do their state migrations as one version bump (version 2) if they
land in the same release, or one bump each otherwise.

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
   rate and block size, re-prepare and re-delay `dryDelay`, call
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
5. **State.** `AudioProcessorValueTreeState::replaceState` leaves parameters
   that are missing from the loaded tree at their current value, not their
   default. A version-1 session or preset loaded after the user picked 8192
   would therefore stay at 8192. Bump the state `version` attribute to 2, and
   in `setStateInformation` set the new parameter to its default when loading
   version 1. Check the factory presets load at 2048.
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
   skill) near the gate controls, or in the region popup's header if space is
   tight. Show milliseconds.
4. **State.** The defaults equal the current hard-coded times, so old sessions
   sound the same. Handle them in the same version-1 migration as item 6
   (reset both to their defaults when loading version 1), for the same
   `replaceState` reason.
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

### 9. A ceiling line per region (the gate's mirror)

*Not mandatory. Planned.*

A second dashed line per region, on the same dB axis as the gate. Bins louder
than it are removed, just as the gate removes bins quieter than its own line,
so a region keeps only the part of its band whose level sits between the two
lines. Typical use: send only the quiet material (tails, breath, room noise)
to the delay and leave the loud notes dry. At the top of the range (the
default) the ceiling is off, as the gate is open at the floor.

This is not ducking in the sidechain sense (delays attenuated while the dry
signal is loud): that remains a separate idea.

**Behaviour: cut.** Bins above the line go to zero (with the gate's attack and
release), which mirrors the gate and reuses its code. A *clamp* mode (bins
above the line are brought down to it, a per-band spectral limiter that tames
resonances without leaving holes) could come later as a per-region choice.

**DSP (`fxme::SpectralBandSplitter`, FxmeTools).**

1. Add `ceilingDb` to `fxme::SpectralBand`, defaulting to a value meaning off
   (mirror of `openGateDb`), so other plugins see no change.
2. Replace the binary per-bin target with a product of two curves:

   ```
   target = rise (level; gate, knee) * (1 - rise (level; ceiling, knee))
   ```

   where `rise` goes from 0 to 1 across a window of `knee` dB centred on the
   threshold (a smoothstep; knee 0 is today's hard switch). Attack and release
   keep smoothing the gain towards that target over time, so the knee and the
   timing complement each other.
3. Add `setGateKnee (float db)`, one value shared by both lines.
4. Cost: keep comparing squared magnitudes against precomputed squared edges
   (below the gate knee, above the ceiling knee, fully inside), and compute a
   logarithm only for bins that fall inside a knee. Skip the ceiling entirely
   when it is off, as the gate is skipped when open.
5. A ceiling at or below the gate is an empty window: the region is silent.
   Consistent, and the editor prevents it (automation can still do it).
6. Core test (hard knee reproduces today's gate exactly, ceiling off changes
   nothing, a tone above the ceiling is removed, knee gives a partial gain),
   and an `api-changes.md` entry.

**Editor (`fxme::SpectrumRegionEditor`, FxmeTools).**

1. Opt-in: `setCeilingEnabled (true)`, off by default, so the other plugins
   using this component are unchanged. `Region` gains `ceilingDb`, with a
   `setCeilingRange()` like `setGateRange()`, and `Handle` gains `ceiling`.
2. Drawn dashed like the gate, with a different dash pattern (or dimmer) so
   the two cannot be confused. Its label goes *above* its line and the gate's
   label *below* its line (today it is above), so the two labels never collide
   when the lines are close.
3. **Push on drag.** The dragged line always follows the mouse. If it comes
   within 10 px of the other line, it pushes that line ahead of it, keeping
   the 10 px gap; it stops only when the pushed line reaches the end of its
   range. No memory: dragging back does not return the pushed line.
4. **Hit test by proximity (midpoint rule).** Pixels are not a stable
   constraint: zooming the dB window, automation or a preset can bring the
   lines closer than 10 px (or together). So `hitTestRegion()` must not rely on
   the gap. Within a region's horizontal span, collect every horizontal line
   within `lineGrabPx` of the pointer (gate, ceiling, and the gain line, which
   is on its own axis but can sit at the same height), and take the nearest.
   When the gate and the ceiling are both within reach, use the midpoint
   between them: above it grabs the ceiling, below it grabs the gate. Either
   line can then always be caught as soon as they are a couple of pixels
   apart, whatever the zoom. This replaces today's fixed order (gate tested
   before the gain line).
5. `onRegionChanged` already carries the whole region, so the consumer gets
   the ceiling with no new callback.

**Dede.**

1. A per-region parameter `rgCeil` ("R1 Ceiling", `gateFloorDb` to
   `gateCeilDb`, default at the top, displayed "off" there), and a global
   "Gate knee" (0 to 24 dB, default 0). The knee sits with the attack and
   release of item 7 as a small gate section, rather than in the region popup.
2. `updateBands()` passes the ceiling (mapped to the splitter's off value at
   the top of the range, as the gate is mapped to `openGateDb` at the floor);
   `processBlock` passes the knee.
3. The region popup gets a ceiling field next to the gate.
4. State: same version-2 migration as items 6 and 7, so older sessions load
   with every ceiling off and a hard knee.
5. `setCeilingEnabled (true)` on the region editor, and the info text.

### 10. Hover highlight on the region handles

*Implemented 2026-09-27 in FxmeTools (`SpectrumRegionEditor`), pending test in
Dede. Remove this item once tested and committed.*

When the pointer is over a handle, draw that handle highlighted, so it is clear
which one a press will grab. This matters most when lines are close (gate,
ceiling and gain line at similar heights), and pairs with the midpoint hit
test of item 9.

Half of it exists: `SpectrumRegionEditor::mouseMove()` already stores the
handle under the pointer in `hover` and repaints when it changes, but
`drawRegion()` never reads `hover`. To finish it (FxmeTools):

1. Pass the hover state to `drawRegion()` and, for the hovered handle only,
   draw it emphasised: the gate (and later the ceiling) line thicker and at
   full alpha; the gain line thicker and the gain/pan round handle brighter
   with a ring; the left or right border thicker. Show that line's value label
   on hover even when the region is not selected (today labels appear only on
   the selected region).
2. Keep the highlight on the handle being dragged for the whole drag, even when
   the pointer moves off it (use the drag's hit, not `hover`, while a drag is
   in progress).
3. Clear `hover` in `mouseExit()`, or the last handle stays highlighted when
   the pointer leaves the component.
4. Colours from the region's own colour (`brighter()` / alpha), no new theme
   entry, so every consumer of the component gets it for free. Mention it in
   `api-changes.md` as a visible change.

### 11. Region popup tabs

*Not mandatory. Prerequisite for items 12 and 13.*

`RegionPanel` (the callout opened by clicking a region) currently holds the
delay settings only. Put them in a `juce::TabbedComponent` with three tabs:
**Delay** (today's content), **Degrade** (item 12) and **Spectral transforms**
(item 13). Tab bar coloured with the region's colour. Remember the last tab
opened (per editor session, not saved) so reopening a region lands where the
user was.

Parameter budget: items 12 and 13 add about 12 parameters per region, so the
total goes from about 110 to about 210. Hosts cope, but keep the
non-essential ones (saturator bias and sag) out unless they earn their place.

### 12. Degrade tab

*Not mandatory. Planned.*

Per region, a fixed chain of three stages, each with its own on/off switch:

| Stage | FxmeTools class | Parameters | State |
|---|---|---|---|
| Saturation | `fxme::Saturator` | on, model (Standard / Dynamic / Triode / ClassAB), drive (dB) | one per channel |
| Bit-crush | `fxme::BitCrusher` | on, bits (1 to 24, fractional) | none |
| Downsample | `fxme::Downsampler` | on, hold factor (1 to 1024, fractional, log range) | one per channel |

All three are core, JUCE-free and realtime-safe; nothing to write in
FxmeTools for the stages themselves.

1. **Placement: after the splitter, before the delay.** Every repeat is then
   equally degraded, and the work is plain per-sample code in Dede's
   processor (8 regions x 2 channels). Degrading inside the feedback loop
   (repeats getting dirtier each time) is nicer but needs the feedback hook of
   item 13b; keep it for later.
2. **Pan after degrade.** The splitter applies gain and pan itself, so a
   region arrives already panned, and a saturator after the pan drives the
   louder side harder (a region panned left distorts more on the left). Add an
   option to `fxme::SpectralBandSplitter` to output each band mono with its
   gain applied but no pan, and apply the constant-power pan in Dede after the
   degrade chain. Keep the gain inside the splitter: it then acts as the drive
   into the saturator, which is what a user expects. FxmeTools change: core
   test, `api-changes.md` entry, default behaviour unchanged.
3. **Drive range.** A single region is much quieter than the full signal, so
   the drive range needs more headroom than a full-band saturator (something
   like 0 to +48 dB).
4. **Aliasing is the point** of bit-crush and downsample, but it spreads
   energy far outside the region (a 100 to 200 Hz region downsampled fills
   the highs). That is what "keep in band" (item 14) is for.
5. Stages that are off cost nothing; skip them rather than running them at
   neutral settings.

### 13. Spectral transforms tab

*Not mandatory. Planned.*

Three effects that work on each region's spectrum inside the splitter's frame,
where the FFT is already paid for: **pitch shift**, **freeze** and **blur**.
They share one piece of machinery, a phase vocoder (per-bin frequency tracking
from the phase advance between hops), which is why they belong together.

**Why spectral and not a time-domain shifter.** Sliding the bins by a fixed
number is a frequency shift (every partial moves by the same number of Hz,
harmonics become inharmonic), not a pitch shift. A pitch shift scales bin
positions (bin k goes to k x ratio) and advances each output bin's phase at
the shifted frequency. Done this way:

- any ratio works: `ratio = 2^(semitones / 12)`, with semitones as a float
  (for example -24 to +24, 0.01 steps), so non-integer intervals and detunes
  are natural;
- no added latency, and little cost: the frequency analysis is done once per
  hop for the whole input (every region shares it), then each region keeps
  one synthesis phase per bin;
- freeze and blur need the same phase tracking.

Limits to know: a phase vocoder adds some phasiness and softens transients,
more so at the splitter's 75% overlap, which is the minimum usable for it.
Phase locking (keeping each peak's neighbouring bins phase-coherent with the
peak, Laroche and Dolson) improves it noticeably and should be part of the
first version. The shift applies once, before the delay, so every repeat has
the same pitch (a climbing shimmer is item 13b).

**Effects and parameters (per region).**

- *Pitch:* on, semitones (float). Shifted content leaves the region's range
  (a 1 to 2 kHz region up an octave comes out at 2 to 4 kHz); that is fine,
  each region has its own output.
- *Freeze:* on (latched). Holds the region's magnitudes as they are when it is
  switched on (after the gate, so it captures what the view shows) and keeps
  advancing the phases, either at the tracked frequencies (tonal) or randomly
  per hop (a smooth wash, the approach of `fxme::SpectralFreeze`, including its
  deterministic `detrand` phase stream). A mode parameter chooses. Reuse the
  approach, not the class: `SpectralFreeze` runs its own FFT. MIDI triggering
  (the plugin already receives MIDI) is a possible later addition.
- *Blur:* on, time (ms). Smooths each bin's magnitude over hops, with the
  tracked phases, so it sounds like a reverb confined to the band rather than
  a smear.

Order inside the frame, per region: gate (and ceiling, item 9) -> freeze or
blur (magnitudes) -> pitch (remapping) -> inverse FFT.

**Structure (FxmeTools core).** Do not grow `SpectralBandSplitter` with all of
this. Add a class, for example `fxme::SpectralBandEffects`: `prepare (numBins,
hop, sampleRate, numBands)` (allocates), `analyse (spectrum)` once per hop,
`processBand (band, frame)` per region. The splitter gets a hook to call it
between the gate and the inverse FFT (a pointer to an interface, no
`std::function` on the audio thread). Testable on its own: a sine shifted by
a known ratio lands at the expected frequency, freeze holds a steady spectrum,
blur of a steady signal is a no-op. `api-changes.md` entry.

### 13b. Shimmer (pitch shift inside the delay feedback)

*Optional. Only after items 12 and 13.*

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
   loop (item 14). Core test: no insert gives bit-identical output.
2. **Parameters (per region):** shimmer on, interval (semitones, float,
   default +12), amount (how much of the shifted signal goes into the
   feedback versus the unshifted one). In the Delay tab.
3. A gentle low-pass in the loop (fixed, around 10 kHz) keeps octave-up
   feedback from piling up as a whistle. The feedback gains are already at
   most 0 dB, so the loop stays stable.
4. `fxme::PitchShifter` smooths its ratio internally, so the interval can be
   moved without zipper noise.

### 14. "Keep in band"

*Not mandatory. Planned; the switch lives in the region column (item 15).*

Keeps a region's processed signal inside the region's frequency range, by
band-passing it along the region's edges after the time-domain processing.
Without it, saturation adds harmonics above the band and bit-crush or
downsample add aliasing everywhere; with it, a region stays a frequency range.

1. **Filter.** High-pass at the lower edge and low-pass at the upper edge, from
   `fxme::Biquad`, per channel: two biquads in series each side for 24 dB per
   octave (12 dB per octave is too leaky for this). Coefficients recomputed
   when the edges move (once per block, only if they changed), with the edges
   smoothed so dragging a border does not zipper. Near the ends of the
   spectrum, drop the stage that is out of range (no high-pass below 20 Hz,
   no low-pass above 20 kHz).
2. **Placement.** After the degrade chain (item 12), before the delay. When
   the feedback hook of item 13b exists, also offer it inside the loop, so
   repeats degraded or shifted in the loop stay in band too.
3. **Spectral pitch (item 13).** The pitch shifter moves the band on purpose,
   so the filter should follow the *shifted* edges (edges x ratio), not the
   original ones. Otherwise keep-in-band would cancel the shift.
4. **Honest limit.** A 24 dB per octave slope is much softer than the
   splitter's spectral mask, so a little of the out-of-band content remains
   near the edges. A region narrower than about an octave will also lose
   level inside the band with a steep filter pair; document it rather than
   compensate.
5. Parameter: per region `rgKeep` (bool, default off). Off costs nothing.

### 15. Region column (left side of the editor)

*Not mandatory. Planned.*

A column on the left of the spectrum view with one row per slot (8 rows),
coloured with the region's colour, each row holding small controls and two
visualisations:

**Buttons, in this order.**

- **Free** (delete). Frees the slot (`rgOn` = false, the existing "slot in
  use" parameter); the slot can be reused by drawing a new region in the
  spectrum view. A momentary action, not a toggle: a small "x" button.
  Same as selecting a region and pressing Delete in the view today.
- **On/off** (bypass). New per-region parameter `rgActive` (default on). Off
  stops the region's input to its delay (the splitter band is disabled, which
  also saves its inverse FFT) but the delay keeps running, so tails decay
  naturally. The region stays in place and editable, drawn dimmed in the
  view.
- **Mute.** New per-region parameter `rgMute`. Silences the region's output
  *after* the delay (both its contribution to the wet sum and its reverb
  send), while all processing continues, so unmuting comes back into running
  tails. Short gain ramp to avoid clicks.
- **Solo.** New per-region parameter `rgSolo`, non-automatable. When any
  region is soloed, every non-soloed region is treated as muted. Several
  solos can be on together. Solo does not touch the dry path (decide whether
  it should mute dry as well; a "solo mutes dry" behaviour is useful for
  auditioning one band, but surprising in a mix).
- **Keep in band** (item 14). `rgKeep`.

The distinction to keep clear in the info text: *free* releases the slot,
*off* stops the input (tails ring out), *mute* stops the output (processing
continues).

Buttons are `fxme::FxmeButton` bound to the parameters (see the
`fxme-juce-controls` skill), except Free, which is a plain action button.
Free-slot rows show dimmed, with only the slot number (no controls enabled).

**Gate meter.** Shows how open the region's gate is: the average of the
per-bin gate gains over the region's bins, 0 (all closed) to 1 (all open),
which with item 9 also reflects the ceiling. The splitter already holds those
gains (`gateGain`); it needs to publish the average per band, once per hop,
through a `std::atomic<float>` read by the GUI timer (FxmeTools change,
`getGateOpenness (band)`). Drawn as a small bar, or a lamp whose brightness
follows the value.

**Stereo VU meter.** Two thin bars per row, the region's output level after
its delay (what it contributes to the wet sum), from one `fxme::VuMeter` per
channel per region (core, atomics, fed from `regionBuffer` in `processBlock`),
displayed with `fxme::VuMeterComponent` (or the pull-style meters in
`FxmeMeters.h`). Measure before mute, so a muted region still shows what it
would contribute (as a DAW shows a muted track); decide if post-mute is
preferred.

**Interaction with the view.**

- Clicking a row selects that region in the spectrum view, and selecting a
  region in the view highlights its row.
- Hovering a row could highlight the region's rectangle in the view (and the
  other way round), in the spirit of item 10.

**Layout.** Column about 150 to 180 px wide, taken from the left of the area
above the global knobs (`resized()` in `PluginEditor.cpp`), rows sharing the
spectrum's height. Raise the minimum editor width (`setResizeLimits`, 760 px
today) to keep the spectrum usable.

**Fix while there (existing behaviour).** Today a region whose slot is freed
is simply skipped in `processBlock`: its delay stops mid-tail (a click), and
its delay line keeps the old content, so the next region drawn in that slot
starts by replaying the previous region's tail. When a slot is freed, fade its
output out over a few milliseconds; when a slot becomes used again, reset its
delay (and its degrade and spectral state) before the first block. The reset
is a buffer clear, realtime-safe but not free (up to 6 s x 2 lines of
samples), so do it on the rising edge only.

**State.** New parameters `rgActive`, `rgMute`, `rgSolo`, `rgKeep` go into the
same version-2 migration as items 6, 7 and 9 (older sessions load with every
region on, unmuted, not soloed, keep-in-band off).

## Verifying a change

- Core: `cmake -S lib/FxmeTools/core -B build-core && cmake --build build-core -j2 && ctest --test-dir build-core --output-on-failure`.
- The splitter's levels must keep matching `fxme::SpectrumAnalyzer`'s (see the
  note in `lib/FxmeTools/doc/api-changes.md`), and each region's output must
  be unchanged sample for sample (within float rounding). A core test that
  records the current outputs for a few region layouts, written before
  starting on items 1 to 3, makes that easy to check.
- Then build the plugin, which is the real check.
