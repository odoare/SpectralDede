# Dede: done

Items from `doc/todo.md` that have been implemented, moved here with their
original numbers so references between the two files stay valid. Newest first.
Work that was never a todo item has no number.

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
