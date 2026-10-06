# Dede: done

Items from `doc/todo.md` that have been implemented, moved here with their
original numbers so references between the two files stay valid. Newest first.
Work that was never a todo item has no number.

## New regions over a fully covered spectrum (2026-10-06)

- **Column + button**: a free row of the region column shows a "+" where a
  row in use shows its "×". It creates a region in that slot, an octave
  wide in the middle of the visible frequency range, every setting at its
  default, and selects it.
- **Shift-drag** on the spectrum draws a new region wherever it starts,
  over existing regions too (FxmeTools' `SpectrumRegionEditor`).
- Both share the editor's new `createRegion()` with the plain drag.

## Region presets; new regions start as a slight echo (2026-10-03)

- **Region presets**: the region dock's strip heading has a preset bar
  (beside the region's number, in its colour) with a "..." browser. A region
  preset is everything about a region but its frequency range, on, active
  and solo. User presets go to `FX-Mechanics/Modules/SpectralRegion/Presets`
  (shared by every Dede instance; a preset saved from one region is in
  every region's list at once). Each region's current preset is saved with
  the session and with global presets; editing a region marks its preset
  modified ("*"), and a region preset load marks the global preset
  modified. Built on FxmeTools' new module presets (phase 3 of its
  `doc/local-presets-plan.md`).
- **Seven factory region presets** (`Source/Assets/Regions`, embedded):
  Slap Back, Dotted Echo, Frozen Wash (wash freeze, wide), Stutter Freeze
  (tonal, retriggered at 1/8), Shimmer Climb (Transpose +12 in the loop),
  Crushed Loop (bit-crush and downsample in the loop), Tuned Resonator
  (MIDI: delay times of 1 and 2 periods of the last note, high feedback).
- **New-region defaults**: a slight echo, synced, with a little reverb:
  DAW sync, Time L 0.1875 (dotted 1/16) / Time R 0.375 (dotted 1/8),
  feedback -14 dB, Delay -12 dB, reverb send -18 dB, Dry 0 dB (constants in
  `ParamIDs.h`, to tune by ear). The Delay switch brings Delay back to -12
  dB when it has nothing to remember. A new region shows no preset.
- **Ctrl-drag copy** keeps the original's region preset name, and its
  modified mark.

## User presets in the FX-Mechanics folder (2026-10-03)

Dede's user presets now live in `<user data>/FX-Mechanics/Dede/Presets`
(`~/.config/FX-Mechanics/Dede/Presets` on Linux) instead of `Dede/Presets`.
On its first start the plugin copies the presets of the old folder across
(once: a `.imported-to-FX-Mechanics` marker is left there; nothing is moved
or deleted). Phase 2 of FxmeTools' `doc/local-presets-plan.md`.

## Preset browser button in the shared preset bar (2026-10-03)

The top bar's "..." (the full preset browser in a callout) is now the
FxmeTools preset bar's own optional button, not Dede's: the bar is 236 px
wide with the button inside it, and `presetBrowserButton` /
`showPresetBrowser()` are gone. Same browser, same size (320 x 380), same
anchor. Phase 1 of FxmeTools' `doc/local-presets-plan.md` (region presets
come in its phase 3).

## Tooltips, with an on/off switch (2026-10-04)

- **`Source/Tooltips.h`**: every hover-help string, one namespace per pane
  (`dede::tips::bar`, `column`, `global`, `dynamics`, `spectral`, `delay`,
  `degrade`, `strip`), as in SuperMoTo and ModalDish. The tips that were
  written inline moved there; new ones on the level knobs, the reverb and
  engine controls, every Dynamics, Delay, Spectral and Degrade knob, box and
  stage switch.
- **"?" in the global controls** (left of the "i"): turns the hover help on
  and off. Saved machine-wide (`Source/AppSettings.h`,
  `FX-Mechanics/Dede.settings`: `~/.config` on Linux, Application Support
  on macOS, `%APPDATA%` on Windows), so it holds across instances and
  sessions. The editor's tooltip window asks it before every tip, which
  covers the dock and the callouts too.

## Branding, window size, more factory presets (2026-10-04)

- **Window**: opens at its minimum size, 1080 x 660 (was 1170 x 800).
- **Name**: shown as "Dédé" (top bar, help); file-system names (preset
  folders, the product) stay "Dede". `theme::displayName`.
- **Icon** (`Assets/icont.png`) centred in the top bar's free space, between
  the description and the preset bar (`fxme::TopBar::setDecoration`).
- **Splash** (`Assets/splash.jpg`, `fxme::SplashOverlay`), with the
  FX-Mechanics logo and a link to fx-mechanics.com under it: 3 s the first
  time the editor opens in a plugin instance (a flag on the processor, not
  saved), and, until clicked, when the top bar's logo or icon is clicked.
  `Assets/icon.jpg` is not used but is embedded (the assets are globbed):
  remove it if it is not wanted.
- **Eight global factory presets**, generated from Init (only their regions
  and a few global settings changed; the regions' own Dry at -inf since the
  global Dry carries the band): Sub Keeper, Drum Splitter, Transient Echo,
  Freeze Pad, Harmonic Resonators (MIDI), Tape Echo, Barber Pole, Stutter
  Highs.
- **Five region presets**: Haas Widener, Barber Pole, Octave Down Bloom,
  Tape Echo, Transient Echo (files of the last three prefixed `Region_`:
  binary-data names come from file names, and the global presets have the
  same names).
- All of them drafted from the parameters, not yet tuned by ear; the gate
  level of the two Transient Echo presets (-30 dB) depends on the input.

## New regions: Dry at 0 dB, no delay (2026-10-03)

- **Defaults swapped**: a region's `dry` now defaults to 0 dB (was -inf) and
  its `dly` to -inf (was 0 dB). A freshly drawn region is heard as its own
  band, processed, with no repeats until Delay is turned up (or its Delay
  switch is switched on, which brings it to 0 dB).
- **A drawn region starts from every default**: the slot's parameters are
  all put back to their defaults before its range is set (it used to keep
  whatever the slot held: an earlier region's delay and effects, or a
  preset's unused slot). A Ctrl-drag copy still takes the original's.
- The region's Dry adds to the global Dry, which carries the whole input:
  with both at 0 dB the band is heard twice (about +6 dB over its range).
  Turn the global Dry down to hear only the regions.
- No state version bump (unreleased). Presets keep their stored values.

## Copy-drag, magnetic moves, double-click reset (2026-10-02)

- **Magnetic borders when moving a region**: the nearer of its two borders
  sticks to another region's border within the same 8 px as a dragged border,
  the band keeping its width, with the same guide line.
- **Ctrl+drag a region copies it**: on the first move, every setting of the
  region but its solo goes to a free slot (`on` set last), and the copy is
  what is dragged, magnetic too, so it can be laid right beside its original.
  Ctrl+click alone copies nothing; nothing happens with no free slot. The
  cursor shows a copy while Ctrl is held over a region.
- **Double-click resets**: on the gate or the ceiling line (back to off), the
  gain line (0 dB) or the pan handle (centre), as one host gesture. Elsewhere
  in the plot a double click still resets the view.
- **FxmeTools:** `SpectrumRegionEditor` gains `onRegionDuplicate` and
  `onHandleDoubleClicked`, and snaps a region moved whole.

## Region settings docked under the spectrum; output strip (2026-10-01)

The region popup (a callout) closed on any click outside it, so a gate or
ceiling line could not be dragged with the Dynamics knobs in sight. It is now
docked under the spectrum and follows the selection (the view, the column
and the dock share it); with nothing selected the dock says how to select or
draw a region. The tab last shown is kept from one region to the next.

- **Output strip**, on the right of the dock, always in view whatever the
  tab: Dry, Delay, Reverb send. They left the Delay tab, which keeps the
  times, their mode (now captioned, the readout under it) and the feedback.
  Gain and pan stay on the spectrum only.
- **Gain and pan dragged separately**: the solid line moves the gain only
  (up and down), the round handle on it the pan only (left and right). Either
  used to move both, so setting one nudged the other. In FxmeTools,
  `SpectrumRegionEditor::Handle::gainPan` became `gain` and `pan` (breaking,
  Dede the only consumer); the host gesture covers just the one parameter.
- **Degrade positions** are three small radio buttons (Before, In loop,
  After) stacked left of each stage's knobs, with the explanation as a
  tooltip, instead of a choice box under them: the dock is 24 px shorter.
  Choice boxes in a stage get 1.4 knob widths, so Saturation's Model reads.
- **Switches on Dynamics and Delay**, drawn like the Spectral and Degrade
  ones but with no parameter of their own: Gate and Ceiling follow their
  lines (off at the bottom and at the top), Delay follows the strip's Delay
  level (off at -inf). Switching one off parks its value there; switching it
  on brings it back where it was (remembered per region for the editor's
  life; a line never dragged comes back at -60 or -12 dB, Delay at 0 dB),
  clear of the other line by 6 dB when that one is on. The Gate and Ceiling
  switches name their mode (expander; compressor, limiter or cut) in place of
  the old headings. Off, a tab's controls are dimmed, still editable.
- **The strip** (region number, "Output", its three knobs) sits level with
  the tabs' switches and knobs instead of the tab bar.
- **Window**: default 1170 x 800 (was 620 high), minimum 1080 x 660 (was
  950 x 520), so the dynamics row and the strip keep readable knobs and the
  spectrum keeps its height over the dock.
- On and Solo stay in the column only, next to the dock already.

## Rhythmic and wide freeze (2026-09-30)

The freeze was only interesting played by hand or automated. Three new
controls in its stage of the Spectral tab (which now captions its choice
boxes):

- **Trigger** (`frztrg`): Manual (as before: frozen while Freeze is on),
  Retrigger (captures again at every step of the rate), On/off (frozen for
  one step, live for the next; frozen on the even steps). Freeze stays the
  master switch.
- **Rate** (`frzrate`): 1/64 to 1/2, each triplet, straight or dotted, synced
  to the host tempo; default 1/8. The steps count from the host's song
  position while it plays (so they sit on the grid, and a loop or a jump
  lands on the right step), and run on by themselves while it is stopped.
  `processBlock` cuts the block at every step boundary, so a step starts on
  its sample whatever the block size (to the splitter's hop, ~11 ms at 2048).
- **Width** (`frzwid`, 0 to 100 %): how far the frozen left and right
  channels drift apart in phase. Tonal: one random offset per partial, fixed
  (still in tune, same level on each side). Wash: one per bin and per hop,
  independent channels at 100 %. Stereo bands only (a mono band has no second
  channel to move). Default 0, the previous sound.
- **FxmeTools:** `SpectralBandEffects` gains `Settings::freezeWidth`,
  `Settings::keepTracking` (the analysis runs while the band is idle, so an
  On/off freeze captures on its step and in tune rather than one hop late;
  alone it changes nothing, bit for bit) and `retriggerFreeze (band)` (a new
  capture on the next frame, synthesis phases carried on). Core tests: no
  width keeps a mono image mono; full width takes a five-partial tonal freeze
  to a correlation of 0.38 at the same level and pitch on each side, a wash
  to about 0; a wide wash reproduces exactly; a retrigger swaps a held
  1500 Hz for the new 1000 Hz input with no gap.
- **Presets** re-saved with the three new parameters (at their defaults).

## Freq shift stage; Shift renamed Transpose (2026-09-29)

- **Freq shift**, a new Degrade stage (the Bode frequency shifter): every
  frequency moved by the same number of Hz, -2 to +2 kHz, cubic knob, default
  +30 Hz, positionable like the others (Before, In loop, After). In the loop
  the repeats spiral up or down. Parameters `fshon`, `fshhz`, `fshpos`. The
  chain order within a position is now saturation, bit-crush, downsample,
  transpose, freq shift.
- **Shift is now Transpose**, so the two cannot be confused: Transpose is a
  musical interval (semitones, a ratio), Freq shift an offset in Hz. Its
  parameters became `trnon`, `trnst`, `trnpos` (were `shfon`, `shfst`,
  `shfpos`); the Spectral tab's Pitch keeps its name.
- **FxmeTools:** `fxme::FrequencyShifter` (new, core): a Hilbert pair of
  all-pass chains (Niemitalo's coefficients) and a single-sideband mix with a
  quadrature oscillator; mono or stereo (one oscillator), no latency. Core
  test `CoreFrequencyShifterTests`: a 1 kHz tone shifted +100, -250 and
  +1000 Hz lands where it should at its own level (to 0.01 dB); its mirror is
  at least 44 dB down from 50 Hz to 15 kHz; identical channels stay
  identical. (A first version shifted the wrong way; the test caught it.)
- **Presets** re-saved: the three keys renamed, the three new parameters in
  (403 each).

## Per-region dynamics: expander and compressor around the two lines (2026-09-29)

The global gate knee, attack and release are gone; each region has its own
dynamics, in a new **Dynamics** tab, the first of the region popup. The tabs
are now in signal order: Dynamics, Spectral, Delay, Degrade. It replaces the
planned detector smoothing (todo item 8, dropped).

- **The two lines keep their role and gain a response.** Under the gate
  (dashed line): a downward expander, Ratio from 1:1 to 1:20 and then
  infinity (a hard gate), with a Range (how far down it goes, to -inf).
  Over the ceiling (dotted line): Ratio from 1:1 to 20:1, then infinity (a
  limiter), then **cut** (mute over the line, the original ceiling). Each
  line has its own Knee, Attack and Release. The defaults (hard gate, range
  -inf, cut, no knee, 5 and 80 ms) are the original behaviour, so presets
  sound as before.
- **Spectral engine:** per bin, each bin's gain following its own level (a
  spectral expander / compressor), once per hop; each bin now carries two
  smoothed gains (gate and ceiling, their own times). **Live engine:** on the
  band's level, per sample: a classic multiband compressor / expander.
- **UI:** the tab shows the two groups side by side (Gate: Ratio, Range,
  Knee, Attack, Release; Ceiling: Ratio, Knee, Attack, Release), each dimmed
  while its line is parked at the end of its range (off), the ceiling's
  heading naming its mode (compressor, limiter, cut). The popup is one knob
  wider. The Gate section of the global controls is gone.
- **FxmeTools core:** `BandDynamics.h` (new): `fxme::BandDynamics` (the
  settings) and `fxme::DynamicsCurve` (the static curves, a logarithm only
  where the answer is not 0 or 1). `SpectralBandSplitter::setBandDynamics`,
  `FilterBankSplitter::setBandDynamics`, `BandGate::setDynamics`; the global
  `setGateKnee` / `setGateTimes` remain and set every band. Core test
  `CoreDynamicsTests`: the curves (expander 1:2 and 1:4, range, hard gate,
  compressor 4:1, limiter, cut, 12 dB knees continuous and monotonic); a
  live 4:1 compressor takes a tone 12 dB over to -8.99 dB, a live 1:2
  expander a tone 10 dB under to -10.00 dB; the spectral compressor lands
  on the three-bin prediction to 0.01 dB. All earlier tests unchanged.
- **Parameters:** `gtrat`, `gtrng`, `gtkn`, `gtatk`, `gtrel`, `clrat`,
  `clkn`, `clatk`, `clrel` per region; `gateKnee`, `gateAtk`, `gateRel`
  removed. Presets re-saved with the per-region values equal to the old
  global ones (379 parameters each).
- The makeup gain is the region's gain handle.
- **Renamed the same day:** `fxme::BandGate` is now
  `fxme::BandDynamicsProcessor` (it compresses too); older entries here keep
  the old name.
- **Fixed the same day:** the ceiling's attack and release were swapped
  (its gain followed the attack when rising, as the gate's does, where for a
  ceiling rising is letting go). A longer release made the compressor
  compress less. Now the ceiling pulls down on its attack and recovers on its
  release, in both engines, the cut included; a core test checks a 2 ms /
  500 ms compressor pulls down in 2.1 ms and lets go in 500 ms.

## Touching regions rebuild the dry signal exactly; sharper attacks (2026-09-29)

Question: why do regions covering the whole spectrum not sound like the dry
signal, with blurred attacks, when the live engine almost does? Measured on a
click through 8 touching regions, gates open: the spectral sum had -24 dB of
energy *before* the click, as much as after (pre-echo spread over the 43 ms
window), and only 0.82 of its peak. Two causes, both in the splitter's masks:

- each region faded its two outermost bins inside itself, so two neighbours
  summed to 0.25 + 0.75 per side instead of 1: a notch of a few bins at every
  border, and the STFT's filtering being zero-phase within a window, each
  notch rang before the attack as much as after;
- the ends of the range cut at 20 Hz and 20 kHz with near brick-wall edges,
  which rang too (at 20 kHz, hardly audible, but most of the peak loss).

The live engine's filters are causal: they smear phase after the attack,
never before it, which the ear forgives.

Fix (FxmeTools `SpectralBandSplitter`): the edge crossfade is centred on the
edge, a band's gain being its lower edge's step minus its upper edge's, so
neighbours sum to exactly 1; a hard edge gives its boundary bin to one band
only; edges at 20 Hz and 20 kHz reach DC and Nyquist. Now the click comes
back at 1.000, nothing before it (-142 dB), total error -136 dB. New core test
(group 11): 8 touching bands rebuild a click and noise to -137 dB at 1024,
2048 and 8192 points, tapers 0, 2 and 4.

What remains is what is asked for: a gate or ceiling that closes bins, or a
spectral effect, still spreads its change over the window, before and after.
Smaller FFT sizes shorten it; the live engine avoids it.

## Stereo bands, detected in mono (2026-09-29)

The regions no longer have to be mono. A global **Bands** choice (Stereo, the
default, or Mono as before) and a **Pan law** for stereo bands (Balance, the
default, or Stereo pan) are in the Engine section of the global controls.

- **Detection stays mono.** Every decision is taken on the sum of the two
  channels, the signal the view shows: per-bin gates and ceilings (the
  splitter forms the sum's spectrum as the average of the two transforms, no
  extra FFT), the live band gate, and the spectral effects' analysis. The
  gains found are applied to both sides.
- **Spectral effects in stereo:** peaks are found on the sum of both
  channels' magnitudes (so one-sided content counts), frequencies are tracked
  on the sum, and each side is resynthesised with its own magnitudes and its
  own phase offset to the sum; freeze and blur keep both. A pure side signal
  (cancelling in the sum) takes the left channel as its phase reference.
- **Degrade:** the stages before the delay are stereo too; the saturator's
  rails and its auto-gain are linked across the two sides.
- **Pan:** a 2 x 2 matrix per region. Mono bands: constant power as before.
  Stereo: centred is unity on both sides (a centred mono source is 3 dB
  louder than with mono bands); Balance turns the far side down, Stereo pan
  folds it into the near one.
- **FxmeTools:** stereo input for `SpectralBandSplitter` and
  `FilterBankSplitter` (`prepare (..., numInputChannels)`,
  `process (left, right, n)`), `SpectralBandProcessor::processBandStereo`
  (with `SpectralBandEffects` overriding it), `BandGate::processLinked`,
  `LevelMatcher::processFrame`, `Saturator::processSample (x, railDemand)`.
  The mono paths are unchanged: the mono splitter matches the previous one
  to float rounding and the mono effects bit for bit. New core test
  `CoreStereoBandsTests`: identical channels give exactly the mono result
  (both engines, and through a pitch shift); with one channel only the gate
  compares the sum; a source 6 dB louder on the left comes out of a pitch
  shift 6.02 dB louder on the left, the sides still 0.94 correlated; the
  balance law.
- **CPU:** stereo about doubles the splitter (8 regions: Spectral 1.35 % to
  2.55 % of a core, Live 1.40 % to 2.85 %).
- **Presets:** `bands` and `panlaw` added. The four sound presets are set to
  Mono, so they sound exactly as designed; Init is Stereo. Re-save them in
  Stereo once they have been listened to that way.

## Degrade positions, Shift, and 13b (shimmer); keep in band removed (2026-09-29)

- **Positions.** Each degrade stage (saturation, bit-crush, downsample, and
  the new Shift) has a selector under its controls: **Before** the delay (on
  the mono band, as before; the region's Dry hears it), **In loop** (on
  everything written into the delay lines, so the first echo once and every
  repeat again), or **After** the delay (on the repeats). New parameters
  `satpos`, `crshpos`, `dspos`, `shfpos`, default Before, so nothing changes
  by default. Each region's degrade state is now stereo (two of each stage,
  one stereo shifter), with the stages placed per block
  (`beginDegradeBlock`) and run per frame (`degradeFrame`).
- **Shift** (`shfon`, `shfst`, -24 to +24 semitones, default +12): a
  time-domain pitch shift, new stage on the Degrade tab. In the loop it is
  the shimmer of todo item **13b**, done this way: each repeat climbs by the
  interval. Its latency (about 20 ms) is taken off the delay time when it is
  in the loop, so the repeats stay on time.
- **FxmeTools.** `fxme::SplicePitchShifter` (new, core): the free-running
  `fxme::PitchShifter` measured badly at an octave (a sine becomes a comb of
  lines around the target, the target itself sometimes 60 dB down), so the
  new one aligns each splice by cross-correlation (coarse then fine search)
  and crossfades; stereo with shared splices; a clean delay at 0 semitones.
  `fxme::StereoCrossDelay` gains a per-sample loop insert (a template
  overload of `process`/`processSample`, zero cost when unused). Core tests
  (`CoreDelayInsertTests`): a neutral insert changes nothing sample for
  sample; the insert acts on the first echo and every repeat; 0 semitones is
  an exact delay; +-7 and +-12 semitones land on the target at 220, 440 and
  523 Hz (spectral peak within 0.5 %); identical channels stay identical.
- **Keep in band removed**, as asked: it mostly took the degrade effects away
  again. The `keep` parameter, the K button in the region column, the filters
  and the help text are gone.
- **Presets** re-saved: `keep` out, the six new parameters in (308 each).
- **Not compensated:** a Shift before or after the delay delays that path by
  about 20 ms (the band and its Dry before, the repeats after). A delay time
  under about 20 ms with a Shift in the loop repeats a little late (the line
  cannot go under 1 ms).

## Held maxima, to place the gates (2026-09-29)

Not a todo item. A visual aid for setting the gate (and ceiling) against the
loudest the sound has been lately, rather than against a moving trace.

- **Spectrum (both engines):** a max-hold on the trace, per frequency: each
  point keeps its highest level for the hold time, then falls at 20 dB/s.
  Drawn as a thin, lighter line under the trace. Since the view measures as
  the spectral gates do, the held line is exactly the loudest each bin group
  has been as the gates saw it (at the view's 25 Hz frame rate: at 1024
  points, parts of the signal fall between two frames).
- **Live engine:** in each region, the band level's held maximum as a dashed
  line over the level marker. Fed with sample-accurate peaks from the band
  gates (`takeBandPeakLevelDb`), so a short hit between two GUI frames still
  registers.
- **The hold time** is the view's new "hold" badge (off, 1, 3, 10 s, or for
  good; 3 s when the editor opens). Changing it starts the held levels over,
  which is also the way to clear a held-for-good line.
- FxmeTools: `SpectrumDisplay::setHoldSeconds` and the badge,
  `SpectrumRegionEditor::setRegionLevel (index, db, peakDb)`,
  `BandGate::takePeakLevelDb`, `FilterBankSplitter::takeBandPeakLevelDb`, with
  a core test for the peak.

## 17. Live engine: filter bank, zero latency (2026-09-29)

A second engine beside the spectral one, chosen in the global controls
(`engine`: Spectral, the default, or Live; not automatable). Live cuts the
regions with filters: no latency, transients intact, gates that act on each
whole band, per sample. See `architecture.md`, "Two engines", for the table
of what each gives up.

- **FxmeTools core.** `fxme::BandGate` (new): gate and ceiling on a signal's
  level, with the spectral gate's knee and attack/release, and levels in the
  spectrum trace's convention for a steady tone (a sine of amplitude A reads
  A/2), so a region's lines mean the same in both engines for tonal
  material. Its detector averages the squared band over about a period of
  its lowest frequency (3 to 50 ms). `fxme::FilterBankSplitter` (new): the
  spectral splitter's API (same `SpectralBand` settings, `setApplyPan`,
  `getBandOutput`, `getGateOpenness`), with `EdgeBandPass` then `BandGate`
  per band, latency 0, plus `getBandLevelDb`. Core tests
  (`CoreFilterBankTests`): level convention, gate, ceiling, knee, attack and
  release times, each band equal to `EdgeBandPass` alone sample for sample,
  zero latency, openness and level.
- **The level marker.** In Live the gates compare the band's whole level,
  which reads above the trace for anything noisy. The view then shows each
  region's band level as a shaded column topped by a solid line
  (`SpectrumRegionEditor::setLevelMarkersEnabled`, `setRegionLevel`, opt-in,
  FxmeTools), so the lines are set against what they are compared to.
- **Switching** follows the FFT size's path (`handleAsyncUpdate`, processing
  suspended): `configureEngine()` prepares the engine switched to, sets the
  dry delay to its latency (none in Live, and the dry path then skips the
  delay line altogether) and reports it, and every region is reset, delay
  tails included. Only the active engine runs. An FFT size change in Live is
  applied when switching back.
- **GUI.** Engine combo in the global controls, the FFT size greyed in Live,
  the read-out saying what Live gives up, the help text on both engines; the
  region popup's Spectral tab dimmed in Live and titled "Spectral (off in
  Live)".
- **Presets.** `engine` at Spectral in the factory presets (268 parameters).
- **CPU.** Measured about equal to the spectral engine (about 1 % of a core
  for 8 regions), not the 5 to 10 times less the plan guessed: the spectral
  splitter got much cheaper since (items 1 to 4), and four biquads per band
  per sample are most of the live cost. SIMD across the bands is item 18.
- **Not done, on purpose:** the mixed mode (the spectral engine with the
  latency taken off the delay lines instead of added to the dry), dropped
  from the plan.

## Top bar: logo and glowing hairline (2026-09-29)

The FX-Mechanics logo (`Source/Assets/logo686.png`) is on the left of the top
bar, through `fxme::TopBar`'s logo argument. The cyan hairline under the bar
glows: the editor's `paintOverChildren` draws a soft accent gradient a few
pixels either side of it and redraws the line brighter. The asset glob in
`CMakeLists.txt` is now `CONFIGURE_DEPENDS`, so an image added to
`Source/Assets` is picked up without a manual reconfigure.

## Layout: left column with the levels and an output meter; global controls (2026-09-29)

- **The bottom row is gone**, and the spectrum view takes the full height.
- **Left column**, top to bottom: the region rows, stretched over whatever
  height the rest leaves (about 40 px each at the default size, down from
  about 60), the four levels in a 2 x 2 grid (Dry, Delays, Reverb
  return, Output), and at the very bottom a stereo **output meter**
  (`Source/OutputMeter.{h,cpp}`): horizontal L and R bars, RMS over 100 ms,
  peak markers held for a second then falling, red over 0 dBFS, a scale from
  -48 to 0 dB. The processor measures its output after the trim with two
  `fxme::VuMeter`s: `getOutputLevelDb (ch)` for the RMS, and
  `takeOutputPeakDb (ch)`, which returns the highest peak since the previous
  call, so no peak between two GUI frames is missed.
- **Global controls.** The options callout is renamed "Global controls"
  (`OptionsPanel` became `GlobalPanel`, same files renamed), and the reverb's
  Room, Damping and Width moved into it, in a Reverb section above Engine and
  Gate. Its help text covers the reverb too.
- The minimum window height went from 480 to 520, so the rows, the knobs
  and the meter still fit.

## Saturation keeps its level as the drive rises (2026-09-28)

Raising a region's saturation drive made it quieter. The output was divided
by the drive, which is unity for small signals, but a band that clips is
capped at the curve's rail whatever the drive, so the division then took off
as many dB as the drive added (26 dB quieter at 36 dB of drive on a loud
tone). The division stays (it is instant and right for small signals), and a
new `fxme::LevelMatcher` (FxmeTools core) after it brings the result back to
the band's RMS before the saturator, measured with 200 ms followers. The
output's energy now stays constant from 0 to 48 dB of drive, for every model
and at every level (core test: within 0.01 dB), and the drive only changes
the character.

What it does not change: Class AB's crossover notch still swallows a quiet
band whole at low drive (the output is silent, and the matcher does not
invent a signal); more drive brings it through.

## 16 and 7. Options window, with the FFT size and the gate timing (2026-09-28)

A gear button in the top bar, between the preset browser's "..." and the help
button, opens `OptionsPanel` (new, `Source/OptionsPanel.{h,cpp}`) in a
callout, like the preset browser. The gear is drawn as a path (`GearButton`,
same file), in the accent colour.

- **Engine:** the FFT size (item 6's `fftSize`, a combo box with a
  `ComboBoxAttachment`), and a read-out of what the running size comes to:
  latency in samples and ms, bin width in Hz, and the gate step (one hop).
  It follows the size the processor actually runs, since a change is applied
  asynchronously.
- **Gate:** attack, release and knee, shared by every region. The knee moved
  here from the bottom row, which now holds only the levels and the reverb.
- **Help:** an info button in the panel's corner explains each setting (the
  resolution against latency trade, the silence at a size change, the gate
  step limiting how short an attack can be).

**Item 7.** Two new global parameters, `gateAtk` ("Gate attack", 1 to 100 ms,
logarithmic, default 5 ms) and `gateRel` ("Gate release", 10 to 1000 ms,
default 80 ms), the splitter's own defaults, so nothing changes by default.
`processBlock` passes them to `splitter.setGateTimes()` every block, next to
the knee (two exponentials). They are automatable, like the knee: they shape
the sound and re-allocate nothing. Only `fftSize` is not.

**Presets** recall all of them, the FFT size (and so the latency) included: a
preset recalls the sound as heard. No "load engine settings" switch. Factory
presets re-saved with `gateAtk` 5 and `gateRel` 80 (267 parameters each).

Left for later: the Smoothing section (now item 16b) and the engine mode
(item 17), which both add their controls to this panel.

## 1, 2, 3, 4. Splitter CPU (2026-09-28)

Changes in `fxme::SpectralBandSplitter` (FxmeTools), same API, same output to
float rounding. Measured at 2048 points, 8 regions, 48 kHz, 128-sample
blocks, in % of one core:

| case                        | before | after |
|-----------------------------|-------:|------:|
| 8 wide regions, open        |   1.32 |  0.98 |
| 8 narrow regions, open      |   1.21 |  0.77 |
| 8 regions, gate closed      |   1.70 |  0.54 |
| 1 region                    |   0.41 |  0.24 |
| no region enabled           |   0.26 |  0.12 |

**2. Only touch the bins inside each region.** A region's frame is zeroed
around its bins, and only the bins `kLo..kHi` are copied, masked and gated.
Only the non-negative half is written, since that is all the inverse transform
reads (the mirror writes are gone too). The care point from the todo is
handled by keeping each region's gate gains at 0 outside its bins: when its
edges move, the bins it leaves are cleared, so a bin that enters later starts
closed, as before.

**3. Skip the inverse FFT when a region's frame is silent.** Detected for free
in the mask loop. For it to trigger on a closed gate, a gate gain now snaps to
0 once its release falls under -100 dB (it used to approach 0 forever). A
spectral effect that sounds on its own (freeze, blur tail) is still heard: the
frame is checked again after the effect, and only skipped if still empty.

**4. Remove the modulo from the history copy.** Two straight loops.

**Not in the todo: the output loop.** It ran per sample over all 8 regions,
enabled or not, which with nothing enabled was most of the splitter's cost. It
now runs per region over runs of samples up to the next hop, with a constant
gain when the gain is not gliding.

**1. Two regions per inverse FFT: dropped.** It was written when the inverse
was a scalar complex FFT. Since the move to WDL's real FFT, a real inverse of
2048 points takes about 7.4 µs and WDL's complex one about 12-13 µs, so
pairing two regions into one complex transform saves 2 µs at best, before
packing the two spectra and splitting the result. Not worth the code.

Checks: core test group 10 (a closed gate gives exact zeros; a processor that
sounds on silent input is heard); an A/B harness against the previous splitter
on noise and tones with random block sizes, moving edges, sub-bin and disabled
regions and silence, at 1024 to 8192 points and knees 0 and 12 dB: every
difference at least 130 dB below the signal.

## 6. FFT size as a parameter, and a view that measures as the gates do (2026-09-28)

The splitter's window is now a parameter, `fftSize` ("FFT Size": 1024, 2048,
4096, 8192, 16384 points; default 2048, so nothing changes by default;
non-automatable). Its control goes into the options window (item 16, next);
until then it can be changed from the host's parameter list.

**Applying it.** `prepareToPlay` reads the parameter. A change while running is
scheduled by a parameter listener and applied on the message thread in
`handleAsyncUpdate()`: `suspendProcessing (true)` (which waits for any block in
flight), `configureSpectralEngine (order)` re-prepares the splitter, the
spectral effects and the dry-path delay, and reports the new latency, then
processing resumes. Expect about one window of silence at the switch, and
hosts that only pick up the new latency on transport stop. Region settings
survive (they are re-sent every block, and the gate thresholds depend on the
window size). The GUI reads the splitter from the message thread too, so a
re-prepare can never pull an array from under a meter.

**The view measures exactly as the gates do**, as asked: the relative level of
the trace and the gate and ceiling lines is now the real one.

- **Window size.** The view follows the size the processor actually runs (the
  editor polls `getFftOrder()`, since the change is applied asynchronously).
- **Window and scale.** The analyser used a symmetric Hann window, the
  splitter a periodic one (about 0.004 dB apart at 2048). The analyser now
  uses the periodic one (FxmeTools); a new core test pins that it reads a
  bin-centred tone at exactly the gate's level (0.0000 dB off at all five
  sizes; the old window failed at the small ones).
- **Detector.** Where many bins share one display point (the highs), "avg"
  drew their power average, so a tone the gate lets through could be drawn
  under the gate line. "peak" shows the loudest bin: the trace is over the
  line exactly when some bin there passes. Dede now uses peak.
- **Averaging.** The trace was smoothed over GUI frames; the gate reacts to
  each frame as it is. Averaging is now off.
- All three are locked, with a new opt-in `SpectrumDisplay::setMeasurementLocked`
  (FxmeTools), like the existing FFT-size lock: the badges draw dimmed and
  ignore clicks. If the trace is too jumpy, the honest cure is item 8 (smooth
  the gate's detector, and the view with it), not smoothing the picture only.

**Also.** Factory presets carry `fftSize` at 2048 (265 parameters each).
`architecture.md` (latency, the reconfiguration path, the exact-measurement
list), the help text and `api-changes.md` are updated; in the todo, items 16
and 17 now point at the parts built here.

## Borders stick to other regions' borders (2026-09-28)

Not a todo item. While a region's border is dragged, it sticks to another
region's border once the pointer comes within 8 px of it: it lands on exactly
the same frequency, so the two regions meet with no gap and no overlap, and it
stays there until the pointer moves more than 8 px past. A thin full-height
line marks the wall while it holds.

- In FxmeTools, `SpectrumRegionEditor::setEdgeSnapPixels (px)`, off by default
  so other plugins using the component are unaffected; Dede turns it on at
  8 px. Recorded in `api-changes.md`.
- Targets are both borders of every other active region; the nearest within
  reach wins. The minimum region width still applies after snapping (it moves
  the border not being dragged, so the snapped one stays on the wall).
- Only border drags snap; moving a whole region does not. It could be added
  (snapping whichever of its two borders comes nearest a wall) if wanted.
- Help text updated.

## 15. Region column (2026-09-28)

A column left of the spectrum view, one row per slot in the slot's colour.
A free slot shows only its number; a slot in use shows, left to right:

| Control | Parameter | What it does |
|---|---|---|
| x (Free) | `on` (slot in use) | Deletes the region and releases the slot, which can be drawn again. Momentary. Same as Delete on the view. |
| On | `act` (new, default on) | At the input: off, the band stops feeding the region (the splitter band is disabled, saving its inverse FFT); the delay runs on and its tails ring out. |
| S | `solo` (new, not automatable) | At the output: silences every region not soloed; several can be on. The global dry is kept. |
| K | `keep` (item 14) | Keep in band; the same parameter as on the Degrade tab. |
| gate bar | — | How much of the region's energy its gate and ceiling let through. |
| L / R bars | — | The region's output level (100 ms RMS), before the solo gain. |

**Mute, removed.** The column first had an M (mute) button next to S: an
output-side switch silencing the region at once while its processing went on.
It was removed the same day as redundant with On. What was lost: On acts at the
input, so a region switched off lets its delay tails ring out, whereas a mute
cut them at once; soloing another region still silences one instantly.

The one-letter buttons are `fxme::AccentToggle`s (latching, custom-painted,
made for mute/solo letters) bound with plain `ButtonAttachment`s; all have
tooltips, and the editor gained a `juce::TooltipWindow` for them (there was
none: the preset button's tooltip never showed before either).

**Selection** is shared both ways: clicking a row selects its region on the
view (and gives the view keyboard focus, so Delete acts at once), and
selecting a region on the view highlights its row.

**Dimming.** A region that is off, or silenced by another's solo, is
drawn dimmed on the spectrum (desaturated and darker: the view sets its own
alphas, so the colour itself changes; no FxmeTools change needed). Its row's
chip and meters dim too.

**DSP.**

- Solo and a freed slot's fade-out are one ramped gain per region (5 ms) on
  the region's Dry and Delay, and so on its send.
- **Stale-slot fix.** A freed slot used to stop mid-tail (a click) and keep
  its delay content, so the next region drawn in it started by replaying the
  old tail. Now a freed slot fades out, then stops being processed; when it
  is used again, `resetRegion()` clears its delay lines, saturator,
  downsampler, keep-in-band filter, spectral effects (`resetBand`, new in
  FxmeTools) and meters, and snaps its smoothed levels and pan to the new
  region's parameters.

**Decisions** (the plan left two open, both settled as it suggested):

- Solo does not mute the global dry.
- The meter reads before the output gain, as a DAW shows a muted track: a
  region silenced by another's solo still shows what it would add. It
  measures the region's whole output (Dry plus Delay), which by now is what
  "the region's output" means.
- The gate meter is energy-weighted, not the plain average of per-bin gains
  the plan described: a loud tone passing in a wide region would read near 0
  on an average, since only three bins are open.
- A solo-silenced region is dimmed too, not only one that is off.

**FxmeTools.** `SpectralBandSplitter::getGateOpenness` (4 new checks in
`CoreSplitterTests.cpp`), `SpectralBandEffects::resetBand`, and a shadowing
local removed in `RealFft`; recorded in `api-changes.md`.

**Also.** Layout: the column is 180 px wide, taken from the left of the
spectrum's area; default editor width 980 -> 1170 px, minimum 760 -> 950 px.
Factory presets carry `act` on and `solo` off (264 parameters each).
`architecture.md` (the three switches kept apart, the stale-slot fix,
parameter list) and the help text are updated.

## 14. Keep in band (2026-09-28)

A per-region switch, **Keep in band** (`keep`, off by default), that
band-passes the region back to its own frequency range after the degrade chain.
Saturation adds harmonics above a region, bit-crush and downsample add
aliasing everywhere; with the switch on, a region stays a frequency range.

**Where.** On each region's mono band, right after the degrade chain and
before the pan, so both the region's Dry output and its delay get the filtered
signal. One filter per region (the band is mono there), not one per channel.
The switch sits at the end of the Degrade tab, where it acts in the signal
order (the plan put it in the region column of item 15, which does not exist
yet; that column's button will bind the same parameter).

**FxmeTools (core).** New `fxme::EdgeBandPass` (`dsp/EdgeBandPass.h`,
header-only, in the module umbrella): a band-pass given by its two edges. A
4th-order Butterworth high-pass at the lower edge and low-pass at the upper
one, two `fxme::Biquad` sections each (Q 0.541 and 1.307), 24 dB per octave.
Generic, hence in FxmeTools rather than in Dede.

- The edges glide in log frequency (20 ms), and coefficients are only
  recomputed when an edge actually moved, so dragging a border does not
  zipper.
- An edge at 20 Hz or 20 kHz (or near Nyquist) drops its stage: a region
  spanning the whole range passes untouched, bit for bit.
- A stage coming back after being dropped, and the whole filter when the
  switch comes back on, start from rest at the current edges.
- New test `core/tests/CoreEdgeBandPassTests.cpp`, 7 checks: 0.0 dB in band,
  -51 dB two octaves above, -48 dB two octaves below, pass-through at full
  range, settling after an edge moves.

**With pitch on**, the filter follows the *shifted* range (edges times the
pitch ratio), or it would cancel the shift.

**Limits, documented rather than compensated.** The slopes are far softer than
the splitter's spectral mask, so a little of the spill remains near the edges.
A region narrower than about an octave loses some level at its centre, because
the two slopes overlap: 1.9 dB for half an octave (tested).

**Also.** Factory presets carry `keep` at 0; `architecture.md`, the help text
and `api-changes.md` are updated. In the todo, item 13b notes how keep-in-band
would go inside the delay loop, and item 15's Mute now describes the region's
whole output (Dry and Delay), which predates it.

## 13. Spectral transforms tab: pitch, freeze, blur (2026-09-27)

A third tab in the region popup, **Spectral**, with three effects that work on
the region's spectrum inside the splitter's frame, where the FFT is already
paid for: no extra transform, no added latency. Each has its own switch, all
off by default. A region with all three off is left untouched bit for bit.

| Effect | Parameters | What it does |
|---|---|---|
| Pitch | `pshon`, `pshst` (-24 to +24 semitones, 0.01 steps) | Moves the region's partials by the ratio; fractional semitones work, harmonics stay harmonic |
| Freeze | `frzon` (latched), `frzmod` (Tonal, Wash) | Captures the region's spectrum (after its gate) when switched on and holds it, ignoring the input. Tonal keeps notes as notes; Wash is a smooth random-phase texture |
| Blur | `blron`, `blrms` (20 ms to 5 s, log, default 300 ms) | Smooths each bin's level over time: a reverb-like tail confined to the region |

Order per region: gate and ceiling, then freeze or blur (freeze wins when both
are on), then pitch, then out of the splitter into the degrade chain, the pan
and the delay.

**FxmeTools (core).**

- A hook in `SpectralBandSplitter`: the `SpectralBandProcessor` interface
  (`beginFrame` once per hop with the whole spectrum, `processBand` per band
  between the gate and the inverse FFT), installed with `setBandProcessor`.
- `SpectralBandEffects` (new, header-only, in the module umbrella): one phase
  vocoder shared by every band (frequency tracking once per hop), then per
  band freeze, blur and pitch, and a resynthesis with identity phase locking.
- New test `core/tests/CoreSpectralEffectsTests.cpp`, 23 checks.

**Decisions and findings while implementing.**

- **Pitch shifts peak regions, not bins.** Moving each bin to
  `round (k * ratio)` tears a partial's three-bin lobe apart for non-integer
  ratios (it lost 2 dB at +7 st and 4 dB at +3.33 st). Instead, each peak and
  the bins around it move together by one whole number of bins, and the exact
  new frequency comes from the peak's phase advance (Laroche and Dolson).
  Tested level change is now under 0.7 dB, and the frequency is exact to the
  0.1 Hz for tones on a bin and between two bins, up and down.
- **Blur tails.** Once the input stops, the analysis only sees silence: its
  frequencies are noise and its phases all zero, which pushed the tail to the
  edges of each frame, where the synthesis window removed it. Blur now holds
  each bin's last reliable frequency (while the live level is under a quarter
  of the blurred one) and uses the phase pattern a steady partial has in this
  window (neighbouring bins pi apart). A 500 ms blur keeps a stopped tone
  sounding, as it should.
- **One hop of pass-through when the effects wake up.** Frequencies need two
  consecutive analysed hops; a tonal freeze captured on the first would hold
  notes up to half a bin (about 12 Hz) out of tune. So a band stays untouched
  for that one hop (about 11 ms).
- **Wash level.** Random phases make overlapping frames add as powers, not
  amplitudes; a gain computed from the window (2 at 75% overlap) restores the
  level (tested within 0.1 dB of the live tone). The phase stream is
  deterministic (`fxme::detrand`, keyed on band, frame and bin), so a wash
  reproduces exactly.
- Freeze is a latched parameter, so it is automatable and saved; MIDI
  triggering stays a possible later addition.

**Dede.**

- The Degrade page became a generic `StagesPage` (a row of stages, each a
  switch over its controls), built from a list for both the Degrade and the
  Spectral tabs.
- `updateBands()` passes each region's settings to the effects every block;
  `prepareToPlay` prepares them from the splitter's FFT and hop sizes.
- Factory presets carry the six new parameters at their defaults (240
  parameters each); `architecture.md` and the help text are updated. Item 6
  (FFT size as a parameter) now notes that the effects must be re-prepared
  with the splitter.

## Region output levels: Dry, Delay, and a post-level reverb send (2026-09-27)

Not a todo item: decided in discussion. A region is now a small channel strip.
Its band, gain applied, degraded (item 12) and panned, leaves it two ways, each
with its own level, and the reverb send is taken after both:

```
band -> gain -> degrade -> pan -+-> Dry ------------+
                                +-> delay -> Delay -+-> region output -> Send -> reverb
```

- **Delay** (`dly`, "R1 Delay Level", -inf to 0 dB, default 0 dB): the
  region's repeats into the mix. Added first, as a knob just before Reverb in
  the popup.
- **Dry** (`dry`, "R1 Dry Level", -inf to 0 dB, default -inf): the region's
  own band, undelayed, into the mix. This is what lets the degrade chain act on
  a region's dry sound, not only on its repeats.
- **Send** (`send`, unchanged range): now taken from the region's output,
  *after* Dry and Delay, like a post-fader send. With Delay at -inf the reverb
  gets no repeats from that region; with Dry up it gets the region's band; with
  both down it gets nothing from it. (Briefly, the send was taken from the
  repeats independently of the Delay level, so a region could feed the reverb
  alone; that was reversed on request.)

**Add, not replace (option A of the discussion).** The global dry stays the
untouched input, so a region's Dry adds its processed band *on top of* the
clean one already there. To hear only the processed band, turn the global Dry
down (which also removes everything outside the regions). The alternative
(option B: the splitter also outputs the remainder no region takes, the global
Dry plays that, and each region's Dry replaces its own range) was considered
and not chosen; it would cost one more inverse FFT per hop and make a region's
gain and pan apply to its dry range too.

**Routing.** The regions' Dry outputs are summed on a bus of their own
(`directBuffer`), scaled only by the output trim: not by the global Dry (as
asked, independent of it) nor by the global Delays knob (they are not delays).
The panned band is aligned with the global dry (both come out after the
splitter's latency), so a region's Dry lines up with it.

**Defaults keep the old sound.** Dry at -inf and Delay at 0 dB give exactly the
previous output, send included (the send used to take the repeats at full
level, which is what Delay at 0 dB gives).

**Also.** The popup's Delay tab is now 8 knobs wide (Time L, Time R, Fb L,
Fb R, Cross, Dry, Delay, Reverb), factory presets carry `dly` at 0 dB and
`dry` at -inf, and `architecture.md` (signal flow, the add-not-replace note,
parameter list) and the help text are updated.

## 12. Degrade tab (2026-09-27)

Each region's band now goes through a degrade chain before its delay:
saturation, then bit-crush, then downsample, each with its own switch, all off
by default.

**Signal path.** The splitter now outputs each band mono, gain applied but not
panned (`setApplyPan (false)`, new in FxmeTools); Dede runs the chain on that
mono band, then applies the constant-power pan itself (smoothed, 20 ms, as the
splitter did), then the delay. Panning after the chain keeps a saturator from
driving the louder side of a panned region harder, and it needs one instance
of each stage per region instead of one per channel. With every stage off the
sound is as before.

**Stages** (FxmeTools core classes, reused as they are):

| Stage | Class | Parameters |
|---|---|---|
| Saturation | `fxme::Saturator` | `saton`, `satmod` (Standard, Dynamic, Triode, Class AB), `satdrv` (0 to 48 dB, default 12) |
| Bit-crush | `fxme::BitCrusher` | `crshon`, `bits` (1 to 24, fractional, default 8) |
| Downsample | `fxme::Downsampler` | `dson`, `dsf` (hold length 1 to 1024, logarithmic, default 4) |

Decisions made while implementing:

- **Auto-gain on the saturator.** Its output is divided by the drive gain,
  which makes it unity for small signals: the drive sets how hard the band
  clips, not how loud it comes out. Without it, a quiet region driven by
  +24 dB would come out near full scale, straight into the delay feedback.
- **Bias and sag fixed per model**, not parameters (as planned). Sag 0.5 for
  every model but Standard: without sag, "Dynamic" is exactly "Standard".
  Class AB gets an overlap of -0.1: at 0 its curve is exactly tanh, and the
  negative overlap is what gives it its crossover notch.
- **Drive smoothed per sample** (20 ms), through a new linear setter,
  `Saturator::setDriveGain`, so automating it does not zipper.
- A stage switched back on starts clean: the saturator's DC blocker and sag,
  and the downsampler's hold, are reset on the off-to-on edge, and the drive
  starts at its value rather than gliding to it.
- Switching a stage is a hard switch (no crossfade); expect a small click when
  toggling on a loud band.

**FxmeTools.** `SpectralBandSplitter::setApplyPan` (with 3 new checks in
`core/tests/CoreSplitterTests.cpp`: pan on still pans, pan off gives identical
channels at the band's own gain) and `Saturator::setDriveGain`, both additive,
recorded in `api-changes.md`.

**Also.** Factory presets carry the seven new parameters at their defaults;
`architecture.md` (signal flow and parameter list) and the help text are
updated.

## 11. Region popup tabs (2026-09-27)

`RegionPanel` (the callout opened by clicking a region) is now a
`juce::TabbedComponent` with two tabs, **Delay** (the previous content,
unchanged) and **Degrade** (item 12). The **Spectral transforms** tab arrives
with item 13; the structure is ready for it (one page class per tab in
`Source/RegionPanel.cpp`).

- The title is now "Region N" (it said "Region N delay").
- Tabs styled from the theme: dim text for the back tabs, the region's colour
  for the front one and its outline.
- The last tab shown is remembered by the editor (`lastRegionTab`), per editor
  session and not saved, so the next region opened lands on the same tab.
  Adding the first tab selects it, so tracking is only switched on once the
  tabs are built and the remembered one restored.
- On the Degrade page, the controls of a stage that is off are dimmed but stay
  editable, so a stage can be set up before it is heard.

## 9. A ceiling line per region, and a gate knee (2026-09-27)

A second dotted line per region on the gate's dB axis: bins louder than it are
removed, as the gate removes bins quieter than its line, so a region can keep
only the level window between the two (the quiet tails and room of a sound
without its loud notes, say). At the top of its range it is off, which is the
default. A global knee turns both switches into smooth transitions.

**DSP, `fxme::SpectralBandSplitter` (FxmeTools core).**

- `SpectralBand::ceilingDb`, last in the struct so existing
  brace-initialisation keeps its meaning; off at or above `offCeilingDb`.
- The per-bin target is `rise (gate) x (1 - rise (ceiling))`, where `rise` is
  0 below a line's lower knee edge, 1 above its upper one, and a smoothstep
  over the level in dB in between. Edges are held as squared magnitudes, so a
  logarithm is only taken for bins inside a knee. A hard knee reproduces the
  old gate exactly.
- `setGateKnee (db)`: shared by both lines and every band, recomputes each
  band's edges, returns at once for an unchanged value.
- Behaviour chosen: *cut* (bins above the ceiling go to zero, through the
  gate's attack and release). A *clamp* mode (bins brought down to the line, a
  per-band spectral limiter) was left for later.
- New core test `core/tests/CoreSplitterTests.cpp` (13 checks on a bin-centred
  tone): the default and an explicit "off" ceiling are sample-identical, gate
  and ceiling pass and remove as expected, an empty window passes nothing, a
  24 dB knee gives a partial gain on either line, and a knee set then taken
  back to zero is exactly the untouched hard switch.

**Editor, `fxme::SpectrumRegionEditor` (FxmeTools module).**

- Opt-in with `setCeilingEnabled (true)`, plus `setCeilingRange()`;
  `Region::ceilingDb` (last in the struct) and `Handle::ceiling` (last in the
  enum). Other consumers see no ceiling.
- Drawn dotted (the gate stays dashed). With the ceiling enabled, the gate's
  label sits under its line and the ceiling's over its own; the ceiling reads
  "ceiling off" at the top of its range.
- **Push on drag:** the dragged line follows the pointer and pushes the other
  one ahead of it, 10 px apart (converted to dB at the current zoom), until the
  pushed line reaches the end of its range. The pushed line starts from where
  it currently is, so it does not spring back.
- **Hit test by proximity** (option 2 of the plan): within a region, the
  nearest horizontal line in reach wins (gain line, gate, ceiling); when the
  gate and the ceiling are both in reach, the pointer's side of their midpoint
  picks the upper or the lower one. This replaced the fixed order in which the
  gate always won, for every consumer.

**Dede.**

- Parameters: per region `ceil` ("R1 Ceiling", `gateFloorDb` to `gateCeilDb`,
  default at the top, displayed "off" there) and global `gateKnee` ("Gate
  knee", 0 to 24 dB, displayed "hard" at 0).
- `updateBands()` maps a ceiling at the top of its range to the splitter's
  off value (as a gate at the floor maps to `openGateDb`); `processBlock`
  passes the knee before updating the bands.
- Editor: region and parameters mapped both ways, new regions start with the
  ceiling off, a gate or ceiling drag is one host gesture on *both*
  parameters (either may be pushed), a Knee knob in the bottom row just left of
  the reverb group (where item 7's attack and release will join it), and the
  help text.
- The plan's "ceiling field in the region popup" was dropped: the popup has no
  gate field either (everything drawn on the spectrum is edited there).
- Factory presets: `gateKnee` and every `rgN_ceil` added at their defaults.

---

## 10. Hover highlight on the region handles (2026-09-27)

In FxmeTools, `fxme::SpectrumRegionEditor`
(`lib/FxmeTools/FxmeTools/components/SpectrumRegionEditor.{h,cpp}`); nothing
changed in Dede's own code. Recorded in `lib/FxmeTools/doc/api-changes.md` as a
visible change with no API change.

**Why.** When lines sit close together (the gate line and the gain line today,
the ceiling of item 9 later), it was not clear which one a press would grab.

**What it does.**

- The handle under the pointer is drawn emphasised, in the region's own colour
  made brighter: a border wider, the gate line thicker, the gain line thicker
  with a ring around the gain/pan disc. It works on every region, not only the
  selected one.
- The emphasised handle shows its value: the frequency for a border (inside
  the region, under its label), the level for the gate, the gain and the pan
  (`C`, `L30`, `R45`) for the gain handle. Such a label may extend past a
  narrow region's borders; it is kept inside the plot.
- During a drag, the dragged handle keeps the emphasis even when the pointer
  moves off it, and no other region shows any. On release the emphasis goes
  back to whatever is under the pointer.
- The region owning an emphasised handle, when it is not the selected one, is
  drawn last, so another region's lines cannot hide the handle about to be
  grabbed.

**How.**

- The component already stored the hovered handle (`hover`, set in
  `mouseMove`) and repainted when it changed, but `drawRegion()` never read it.
  `drawRegion()` now takes the handle to emphasise, chosen by
  `emphasisFor (index)`: the dragged handle during a drag, otherwise the
  hovered one, nothing while a new region is being drawn.
- `mouseExit` is now overridden (chaining to `SpectrumDisplay::mouseExit`) and
  clears the hover; before, the last hovered handle was never cleared.
- `mouseUp` ends the drag and refreshes the hover from the release position
  before calling `onDragEnd` / `onRegionClicked`, so a popup opened by a click
  does not leave a stale highlight behind.
- New helpers `hzText()` and `panText()` for the labels.

**Related.** Item 9's midpoint hit test builds on this. Item 15's row hover
(still to do) could reuse the same emphasis to highlight a region from the
column.
