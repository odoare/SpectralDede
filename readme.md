# Dede

A spectral multi-delay and reverb, by FX-Mechanics.

Dede shows you the incoming spectrum and lets you draw rectangular regions on
it. Each region is a frequency band with its own spectral gate, gain and pan,
feeding its own stereo delay (independent left and right times, left, right and
cross feedback) and a send to a shared reverb. Delay times can be set in
seconds, synced to the host tempo, or tuned to the period of the last MIDI note
received.

Because the split is spectral rather than a bank of filters, a region can be
made to pass only the peaks of its band, or only its noise floor, which is what
the gate is for. See [doc/architecture.md](doc/architecture.md) for the design.

## Using it

The spectrum view is the whole interface.

- Drag on empty space to draw a region. Drag a border to move it, drag inside
  to move the whole band.
- The dashed line is the gate: what rises above it passes, the rest of the band
  is dropped. Drag it up into the trace and only the peaks get through.
- The solid line sets the region's gain (drag it up and down), the round
  handle on it its pan (drag it left and right, across the region's width).
  Each moves only its own value.
- Select a region (click it, or its row in the column) and its settings show
  under the spectrum, where they stay while you drag its lines: tabs for its
  dynamics, spectral effects, delay and degrade stages, and beside them its
  output (its own dry band, its repeats, the reverb send).
- Each region has its own presets (everything about it but its frequency
  range), in the bar over its output knobs; "..." opens the browser. They are
  shared by every Dede instance.
- Ctrl+drag a region to copy it, every setting with it, and place the copy.
  A moved region's borders stick to the other regions' borders, as a dragged
  border does, so a copy can be set right beside its original.
- Double-click a line (gate, ceiling, gain) or the pan handle to reset it;
  the gate and the ceiling go back to off.
- Delete or Backspace removes the selected region. Up to eight at once.

Delay times read three ways, chosen per region: seconds, a proportion of a
whole note at the host tempo (0.25 is a quarter note), or a multiple of the
period of the last MIDI note received. The last one turns a short delay into a
resonator tuned to whatever is played, so Dede takes MIDI input while being an
audio effect.

On the view itself, the wheel zooms the level axis and ctrl+wheel the frequency
axis; alt+drag or middle-drag pans, and a double-click resets both. Plain drag
is reserved for drawing regions.

The plugin reports its analysis latency (2048 samples, 43 ms at 48 kHz) to the
host, which compensates for it. The dry path is delayed to match, so nothing
inside Dede is out of step with anything else.

## Building

JUCE is expected as a sibling directory of this repo. FxmeTools is a submodule
and carries WDL as its own nested submodule, so clone recursively.

```sh
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2 --target Dede_VST3
```

Formats built are VST3, AU (macOS) and a standalone application.

Releases are cut by pushing a version tag (`git tag v0.1.0 && git push
--tags`), which builds and publishes Linux, Windows and a macOS universal
binary. The same workflow can be run by hand from the Actions tab to test the
builds without publishing anything.

The factory presets in `Source/Assets` are generated, not hand-written:

```sh
DEDE_ASSETS=Source/Assets python3 tools/make_presets.py
```

## Installing

The build does not install anything. Copy the built bundle into the plugin
folder and let the DAW rescan (it caches the module, so a reload is usually
needed too).

### macOS

These plugins are not notarised, so a downloaded bundle is quarantined and the
DAW silently skips it. Clear the flag after copying it in place:

```sh
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Dede.vst3"
```

## Licence

GNU LGPL v3.0 or later.
