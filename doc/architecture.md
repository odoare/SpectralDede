# Dede — architecture

Dede is an FX-Mechanics JUCE audio effect: a spectral multi-delay with a shared
reverb. The user draws rectangular regions on a spectrum view; each region is a
frequency band with its own spectral gate, gain and pan, feeding its own stereo
delay and a send to one common reverb.

This document tracks the plugin as it is built. It is kept in sync with the code
step by step, and is the first thing to read when picking the project back up.

## Status

Step 1 of 5 is in place: the project skeleton, the complete parameter set, the
preset system and the editor shell with a working spectrum view. The audio path
is dry only, so nothing is audible beyond the input.

| Step | Content | State |
|---|---|---|
| 1 | Skeleton, parameters, preset manager, editor shell, spectrum view | done |
| 2 | `fxme::SpectralBandSplitter` and per-region gate, gain, pan | to do |
| 3 | `fxme::SpectrumRegionEditor` (drawing and editing regions) | to do |
| 4 | `fxme::StereoCrossDelay`, delay-time modes, region popup | to do |
| 5 | `fxme::Reverb` over WDL, sends, factory presets, CI | to do |

## Signal flow

```
                        ┌──────────────── dry ────────────────┐
                        │                                     │
  in ──> spectrum tap ──┴─> band splitter ──> region 0 ──> stereo delay 0 ─┐
                              (STFT)      ├─> region 1 ──> stereo delay 1 ─┤
                                          └─> ...                          │
                                                                           ├─> out
                                          per-region sends ──> reverb ─────┘
```

The dry signal is never processed: it is the untouched input, level-trimmed.
Everything the regions do is added on top, so with no region enabled Dede is a
gain stage.

## Where code lives

Anything not specific to Dede goes into the `lib/FxmeTools` submodule under
`fxme::`, and Dede keeps only its own glue. The split for this plugin:

FxmeTools (new or extended, see the steps above):

- `dsp/SpectralBandSplitter.h` — the STFT band split with per-band spectral
  gate, gain and pan. New in step 2. Depends on the WDL real FFT, like
  `dsp/SpectralFreeze.h`, so it is included directly rather than through the
  module umbrella header.
- `components/SpectrumDisplay.h` — extended in step 3 with protected coordinate
  mapping and a paint-overlay hook, so views can be built on top of it without
  copying its grid and trace drawing. Backward compatible.
- `components/SpectrumRegionEditor.h` — new in step 3: rectangular regions over
  a spectrum, with border, gate and gain/pan handles.
- `dsp/StereoCrossDelay.h` — new in step 4: two `fxme::DelayLine`s with a left,
  right and cross feedback matrix.
- `dsp/DelayTimeResolver.h` — new in step 4: one delay value read as seconds, a
  fraction of a whole note, or a multiple of a MIDI note period.
- `dsp/Reverb.h` — new in step 5: a JUCE-friendly float wrapper over
  `WDL_ReverbEngine` (WDL/verbengine.h, Freeverb).

Dede's own `Source/`:

- `ParamIDs.h` — every APVTS identifier and the shared parameter limits.
- `Theme.h` — palette, per-region colours, control styling, geometry.
- `PluginProcessor.{h,cpp}` — APVTS, cached parameter pointers, transport and
  MIDI state, the audio path.
- `PluginEditor.{h,cpp}` — window layout, global controls, preset chrome.

## Parameters

An APVTS parameter set cannot grow at runtime and stay automatable, so Dede has
a fixed pool of `dede::maxRegions` (8) region slots. A slot the user has not
created has its `on` parameter false and costs nothing on the audio thread.
Region identifiers are built as `rg<slot>_<suffix>` (`rg0_flo`, `rg3_fbx`); the
scheme is part of the saved state, so slots are never renumbered and suffixes
never renamed.

Global: `dryLevel`, `wetLevel` (the summed delays), `outLevel`, and the shared
reverb's `revRoom`, `revDamp`, `revWidth`, `revLevel`.

Per region: `on`, `flo`, `fhi` (band borders, logarithmic 20 Hz to 20 kHz),
`gate` (dB, at its minimum the region is fully open), `gain` (dB), `pan`,
`timel`, `timer` (0.001 to 3, meaning set by the mode), `mode` (seconds, DAW
sync, MIDI note), `fbl`, `fbr`, `fbx` (dB, minimum reads as silence), `send`
(dB, to the reverb).

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
buffers are sized for `dede::maxDelaySeconds` (12 s) and the resolver clamps.

## State

`getStateInformation` writes the APVTS state XML with a `version` attribute
from the first release, so later formats can be migrated in
`setStateInformation`. The preset manager stores the current preset name and
dirty flag on the state tree itself, so both round-trip through host sessions
and preset files.

Factory presets are embedded from `Source/Assets/*.xml` via
`juce_add_binary_data`. While that folder is empty the CMake sets
`DEDE_HAS_BINARY_DATA=0` and the processor builds a user-presets-only manager;
adding the first asset flips it without further changes.

## Build

JUCE is expected as a sibling directory (`../JUCE`). FxmeTools is a submodule
under `lib/`, and carries WDL as its own nested submodule, so the checkout has
to be recursive:

```sh
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2 --target Dede_VST3
```

Of the WDL sources only `fft.c` is compiled (for the band splitter); the reverb
engine is header-only. Builds do not install: the built `.vst3` has to be copied
into the VST3 folder and the DAW made to rescan.
