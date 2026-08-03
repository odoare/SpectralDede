# Dede

A spectral multi-delay and reverb, by FX-Mechanics.

Dede shows you the incoming spectrum and lets you draw rectangular regions on
it. Each region is a frequency band with its own spectral gate, gain and pan,
feeding its own stereo delay (independent left and right times, left, right and
cross feedback) and a send to a shared reverb. Delay times can be set in
seconds, synced to the host tempo, or tuned to the period of the last MIDI note
received.

The plugin is under construction. See [doc/architecture.md](doc/architecture.md)
for the design and the current state.

## Building

JUCE is expected as a sibling directory of this repo. FxmeTools is a submodule
and carries WDL as its own nested submodule, so clone recursively.

```sh
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2 --target Dede_VST3
```

Formats built are VST3, AU (macOS) and a standalone application.

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
