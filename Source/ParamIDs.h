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

    // Per-region suffixes (see the file header for the naming scheme).
    inline constexpr const char* rgOn    = "on";      // region slot in use
    inline constexpr const char* rgFLo   = "flo";     // Hz, left border
    inline constexpr const char* rgFHi   = "fhi";     // Hz, right border
    inline constexpr const char* rgGate  = "gate";    // dB, spectral gate floor
    inline constexpr const char* rgGain  = "gain";    // dB
    inline constexpr const char* rgPan   = "pan";     // -1..1
    inline constexpr const char* rgTimeL = "timel";   // 0.001..3, see DelayMode
    inline constexpr const char* rgTimeR = "timer";   // 0.001..3, see DelayMode
    inline constexpr const char* rgMode  = "mode";    // DelayMode choice
    inline constexpr const char* rgFbL   = "fbl";     // dB, left feedback
    inline constexpr const char* rgFbR   = "fbr";     // dB, right feedback
    inline constexpr const char* rgFbX   = "fbx";     // dB, cross feedback
    inline constexpr const char* rgSend  = "send";    // dB, send to the reverb

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
    default sits there: a freshly drawn region passes its whole band. */
inline constexpr float gateFloorDb = -100.0f;
inline constexpr float gateCeilDb  = 0.0f;

inline constexpr float minGainDb = -60.0f;
inline constexpr float maxGainDb = 12.0f;

/** Feedback and send minima are treated as -infinity (silence) by the DSP. */
inline constexpr float minFeedbackDb = -60.0f;
inline constexpr float maxFeedbackDb = 0.0f;

inline constexpr float minDelayValue = 0.001f;
inline constexpr float maxDelayValue = 3.0f;

/** Longest delay any line can be asked for, in seconds: the delay buffers are
    sized for this once, in prepareToPlay. Sync and MIDI-note modes can ask for
    more than maxDelayValue seconds (3 whole notes at a slow tempo), so the
    buffer is generous and the resolver clamps to it. */
inline constexpr float maxDelaySeconds = 12.0f;

} // namespace dede
