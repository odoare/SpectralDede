/*
  ------------------------------------------------------------------------------
    ParamIDs.h

    Every APVTS parameter identifier of Dede, in one place.

    Dede has a fixed pool of maxRegions region slots. A region is a rectangle
    drawn on the spectrum view: a frequency interval, a spectral gate, a gain
    and a pan, feeding its own stereo delay and a send to the shared reverb.
    The pool is fixed because an APVTS parameter set cannot grow at runtime and
    still be automatable; a slot the user has not created simply has its "on"
    parameter false and costs nothing in the audio thread.

    Region parameter IDs are built as "rg<slot>_<name>" (rg0_flo, rg3_fbx, ...).
    They are generated rather than spelled out, but the scheme is part of the
    saved state: never renumber a slot or rename a suffix, or existing presets
    and host automation break.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace dede
{

/** Number of region slots. Raising it is state-compatible (old presets simply
    leave the new slots off); lowering it is not. */
inline constexpr int maxRegions = 8;

/** How a region's delay-time value is interpreted. Stored as a choice index,
    so the order is part of the saved state and only extends at the tail. */
enum class DelayMode
{
    seconds = 0,   // the value is the delay in seconds
    sync,          // the value is a fraction of a whole note (0.25 = a quarter)
    midiNote       // the value multiplies the period of the last MIDI note in
};

inline constexpr int numDelayModes = 3;

namespace ids
{
    // Global
    inline constexpr const char* dryLevel   = "dryLevel";     // dB
    inline constexpr const char* wetLevel   = "wetLevel";     // dB, delay sum
    inline constexpr const char* outLevel   = "outLevel";     // dB, master trim

    inline constexpr const char* revRoom    = "revRoom";      // 0..1
    inline constexpr const char* revDamp    = "revDamp";      // 0..1
    inline constexpr const char* revWidth   = "revWidth";     // 0..1
    inline constexpr const char* revLevel   = "revLevel";     // dB, reverb return

    inline constexpr const char* gateKnee   = "gateKnee";     // dB, gate/ceiling transition
    inline constexpr const char* gateAttack = "gateAtk";      // ms, a bin opening
    inline constexpr const char* gateRelease = "gateRel";     // ms, a bin closing
    inline constexpr const char* fftSize    = "fftSize";      // choice, see fftSizeNames
    inline constexpr const char* engine     = "engine";       // choice, see engineNames

    // Per-region suffixes (see the file header for the naming scheme).
    inline constexpr const char* rgOn    = "on";      // region slot in use
    inline constexpr const char* rgActive = "act";    // bool, the band feeds the region (off: tails ring out)
    inline constexpr const char* rgSolo  = "solo";    // bool, non-automatable
    inline constexpr const char* rgFLo   = "flo";     // Hz, left border
    inline constexpr const char* rgFHi   = "fhi";     // Hz, right border
    inline constexpr const char* rgGate  = "gate";    // dB, spectral gate floor
    inline constexpr const char* rgCeil  = "ceil";    // dB, spectral ceiling
    inline constexpr const char* rgGain  = "gain";    // dB
    inline constexpr const char* rgPan   = "pan";     // -1..1
    inline constexpr const char* rgTimeL = "timel";   // 0.001..3, see DelayMode
    inline constexpr const char* rgTimeR = "timer";   // 0.001..3, see DelayMode
    inline constexpr const char* rgMode  = "mode";    // DelayMode choice
    inline constexpr const char* rgFbL   = "fbl";     // dB, left feedback
    inline constexpr const char* rgFbR   = "fbr";     // dB, right feedback
    inline constexpr const char* rgFbX   = "fbx";     // dB, cross feedback
    inline constexpr const char* rgDry   = "dry";     // dB, the band itself (no delay) into the mix
    inline constexpr const char* rgDly   = "dly";     // dB, delay output into the mix
    inline constexpr const char* rgSend  = "send";    // dB, send to the reverb

    // Degrade chain (saturation -> bit-crush -> downsample), per region.
    inline constexpr const char* rgSatOn    = "saton";   // bool
    inline constexpr const char* rgSatModel = "satmod";  // SaturationModel choice
    inline constexpr const char* rgSatDrive = "satdrv";  // dB
    inline constexpr const char* rgCrushOn  = "crshon";  // bool
    inline constexpr const char* rgCrushBits = "bits";   // 1..24, fractional
    inline constexpr const char* rgDsOn     = "dson";    // bool
    inline constexpr const char* rgDsFactor = "dsf";     // hold length, 1..1024
    inline constexpr const char* rgKeep     = "keep";    // bool, band-pass to the region's range

    // Spectral transforms (pitch, freeze, blur), per region.
    inline constexpr const char* rgPitchOn  = "pshon";   // bool
    inline constexpr const char* rgPitchSt  = "pshst";   // semitones, fractional
    inline constexpr const char* rgFreezeOn = "frzon";   // bool, latched
    inline constexpr const char* rgFreezeMode = "frzmod"; // FreezeMode choice
    inline constexpr const char* rgBlurOn   = "blron";   // bool
    inline constexpr const char* rgBlurMs   = "blrms";   // ms

    /** The APVTS id of region slot `slot`'s `suffix` parameter. */
    inline juce::String region (int slot, const char* suffix)
    {
        return "rg" + juce::String (slot) + "_" + suffix;
    }
}

//==============================================================================
// Shared parameter limits. The DSP and the GUI both read these, so a range is
// changed in exactly one place.

inline constexpr float minFreqHz = 20.0f;
inline constexpr float maxFreqHz = 20000.0f;

/** A gate at its minimum is fully open (nothing is rejected), which is why the
    default sits there: a freshly drawn region passes its whole band. The
    ceiling shares this range, mirrored: at its maximum (gateCeilDb) it is off,
    and that is its default. */
inline constexpr float gateFloorDb = -100.0f;
inline constexpr float gateCeilDb  = 0.0f;

/** Width of the transition around the gate and the ceiling lines. 0 switches
    each bin outright; wider, a bin fades in and out across that many dB. */
inline constexpr float maxGateKneeDb = 24.0f;

/** How fast a bin's gate opens and closes, in ms. Applied once per hop, so
    the resolution is one hop (about 11 ms at 2048 points): an attack under
    that is effectively instant. The defaults are the splitter's own. */
inline constexpr float minGateAttackMs      = 1.0f;
inline constexpr float maxGateAttackMs      = 100.0f;
inline constexpr float defaultGateAttackMs  = 5.0f;
inline constexpr float minGateReleaseMs     = 10.0f;
inline constexpr float maxGateReleaseMs     = 1000.0f;
inline constexpr float defaultGateReleaseMs = 80.0f;

inline constexpr float minGainDb = -60.0f;
inline constexpr float maxGainDb = 12.0f;

/** Feedback and send minima are treated as -infinity (silence) by the DSP. */
inline constexpr float minFeedbackDb = -60.0f;
inline constexpr float maxFeedbackDb = 0.0f;

/** Degrade chain. Drive is generous because a single region is far quieter
    than the full signal; the saturator's output is scaled back by the drive,
    so the knob sets how hard the band clips, not how loud it comes out. */
inline constexpr float maxSatDriveDb     = 48.0f;
inline constexpr float defaultSatDriveDb = 12.0f;
inline constexpr float minCrushBits      = 1.0f;
inline constexpr float maxCrushBits      = 24.0f;
inline constexpr float maxDownsample     = 1024.0f;

/** Order of the saturation model choice; matches fxme::Saturator::Model. */
inline const juce::StringArray saturationModelNames { "Standard", "Dynamic", "Triode", "Class AB" };

/** Spectral transforms. Pitch in semitones, fractional; blur as the time
    constant of the magnitude smoothing. */
inline constexpr float maxPitchSemitones = 24.0f;
inline constexpr float minBlurMs = 20.0f;
inline constexpr float maxBlurMs = 5000.0f;
inline constexpr float defaultBlurMs = 300.0f;

/** Order of the freeze mode choice; matches fxme::SpectralBandEffects::FreezeMode. */
inline const juce::StringArray freezeModeNames { "Tonal", "Wash" };

inline constexpr float minDelayValue = 0.001f;
inline constexpr float maxDelayValue = 3.0f;

/** Analysis window of the spectral band splitter, as an FFT order: 11 is 2048
    samples, about 23 Hz of resolution and 43 ms of latency at 48 kHz. It is
    also the plugin's reported latency, and the window the gate thresholds are
    measured in; the spectrum view follows it so the gate line sits where it
    looks like it sits. Chosen with the `fftSize` parameter; this is its
    default. */
inline constexpr int splitterFftOrder = 11;

/** The `fftSize` choice: 1024 to 16384 points (orders 10 to 14, what both the
    splitter and the spectrum view accept). */
/** The `engine` choice. Spectral: the STFT splitter (per-bin gates,
    spectral effects, one window of latency). Live: a filter bank (per-band
    gates, soft edges, no spectral effects, no latency). */
inline const juce::StringArray engineNames { "Spectral", "Live" };
inline constexpr int liveEngineIndex = 1;

inline const juce::StringArray fftSizeNames { "1024", "2048", "4096", "8192", "16384" };
inline constexpr int minChoiceFftOrder = 10;
inline constexpr int defaultFftSizeIndex = splitterFftOrder - minChoiceFftOrder;

inline int fftOrderForChoice (int index)
{
    return juce::jlimit (minChoiceFftOrder, minChoiceFftOrder + fftSizeNames.size() - 1,
                         minChoiceFftOrder + index);
}

/** Longest delay any line can be asked for, in seconds: the delay buffers are
    sized for this once, in prepareToPlay. Sync and MIDI-note modes can ask for
    more than maxDelayValue seconds (three whole notes at a slow tempo), so the
    buffer has room beyond the parameter's own range and the resolver clamps to
    it. Six seconds is three whole notes at 120 BPM; going much further would
    cost real memory, at two lines per region slot. */
inline constexpr float maxDelaySeconds = 6.0f;

} // namespace dede
