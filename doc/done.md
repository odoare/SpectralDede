# Dede: done

Items from `doc/todo.md` that have been implemented, moved here with their
original numbers so references between the two files stay valid. Newest first.

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
