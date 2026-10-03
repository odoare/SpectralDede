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
#include <limits>

namespace dede
{

/** The module name of region presets: their files' `module`, and their
    folder, FX-Mechanics/Modules/SpectralRegion/Presets (see FxmeTools'
    ModulePresetLibrary). */
inline constexpr const char* regionPresetModule = "SpectralRegion";

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

    inline constexpr const char* fftSize    = "fftSize";      // choice, see fftSizeNames
    inline constexpr const char* engine     = "engine";       // choice, see engineNames
    inline constexpr const char* bandMode   = "bands";        // choice, see bandModeNames
    inline constexpr const char* panLaw     = "panlaw";       // choice, see panLawNames

    // Per-region suffixes (see the file header for the naming scheme).
    inline constexpr const char* rgOn    = "on";      // region slot in use
    inline constexpr const char* rgActive = "act";    // bool, the band feeds the region (off: tails ring out)
    inline constexpr const char* rgSolo  = "solo";    // bool, non-automatable
    inline constexpr const char* rgFLo   = "flo";     // Hz, left border
    inline constexpr const char* rgFHi   = "fhi";     // Hz, right border
    inline constexpr const char* rgGate  = "gate";    // dB, spectral gate floor
    inline constexpr const char* rgCeil  = "ceil";    // dB, spectral ceiling

    // Dynamics around the two lines (see fxme::BandDynamics), per region.
    inline constexpr const char* rgGateRatio   = "gtrat";  // 1..21, 21 = infinite (a gate)
    inline constexpr const char* rgGateRange   = "gtrng";  // dB, bottom = -inf
    inline constexpr const char* rgGateKnee    = "gtkn";   // dB
    inline constexpr const char* rgGateAttack  = "gtatk";  // ms
    inline constexpr const char* rgGateRelease = "gtrel";  // ms
    inline constexpr const char* rgCeilRatio   = "clrat";  // 1..22, 21 = infinite (a limiter), 22 = cut
    inline constexpr const char* rgCeilKnee    = "clkn";   // dB
    inline constexpr const char* rgCeilAttack  = "clatk";  // ms
    inline constexpr const char* rgCeilRelease = "clrel";  // ms
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

    // Degrade chain (saturation -> bit-crush -> downsample -> transpose ->
    // frequency shift), per
    // region. Each stage runs at its own position (see degradePositionNames).
    inline constexpr const char* rgSatOn    = "saton";   // bool
    inline constexpr const char* rgSatModel = "satmod";  // SaturationModel choice
    inline constexpr const char* rgSatDrive = "satdrv";  // dB
    inline constexpr const char* rgCrushOn  = "crshon";  // bool
    inline constexpr const char* rgCrushBits = "bits";   // 1..24, fractional
    inline constexpr const char* rgDsOn     = "dson";    // bool
    inline constexpr const char* rgDsFactor = "dsf";     // hold length, 1..1024
    inline constexpr const char* rgTransposeOn  = "trnon";   // bool, time-domain pitch shift
    inline constexpr const char* rgTransposeSt  = "trnst";   // semitones, fractional
    inline constexpr const char* rgSatPos   = "satpos";  // DegradePosition choice
    inline constexpr const char* rgCrushPos = "crshpos"; // DegradePosition choice
    inline constexpr const char* rgDsPos    = "dspos";   // DegradePosition choice
    inline constexpr const char* rgTransposePos = "trnpos";
    inline constexpr const char* rgFreqShiftOn  = "fshon";   // bool, frequency shift (Hz)
    inline constexpr const char* rgFreqShiftHz  = "fshhz";   // Hz, signed
    inline constexpr const char* rgFreqShiftPos = "fshpos";  // DegradePosition choice  // DegradePosition choice

    // Spectral transforms (pitch, freeze, blur), per region.
    inline constexpr const char* rgPitchOn  = "pshon";   // bool
    inline constexpr const char* rgPitchSt  = "pshst";   // semitones, fractional
    inline constexpr const char* rgFreezeOn = "frzon";   // bool, latched
    inline constexpr const char* rgFreezeMode = "frzmod"; // FreezeMode choice
    inline constexpr const char* rgFreezeTrig = "frztrg"; // FreezeTrigger choice
    inline constexpr const char* rgFreezeRate = "frzrate"; // freezeRateNames choice
    inline constexpr const char* rgFreezeWidth = "frzwid"; // 0..1, stereo bands only
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

/** A region's dynamics (the Dynamics tab), around its two lines.

    Gate (the lower line): a downward expander, `gtrat` from 1 (none) to 20,
    its top (21) meaning infinite, a hard gate; `gtrng` how far down it goes,
    its bottom meaning -inf (silence).
    Ceiling (the upper line): a compressor, `clrat` from 1 to 20, then 21 for
    infinite (a limiter) and 22 for a cut (mute over the line, the original
    ceiling, and the default).
    Each line has its knee (0 to 24 dB), attack and release. The defaults (a
    hard gate, a cut, no knee, 5 ms and 80 ms) are the original behaviour. */
inline constexpr float maxRatio          = 20.0f;
inline constexpr float infiniteRatio     = 21.0f;   // the top of gtrat, a notch of clrat
inline constexpr float cutRatio          = 22.0f;   // the top of clrat
inline constexpr float minGateRangeDb    = -80.0f;  // read as -inf
inline constexpr float maxKneeDb         = 24.0f;
inline constexpr float minAttackMs       = 0.1f;
inline constexpr float maxAttackMs       = 100.0f;
inline constexpr float defaultAttackMs   = 5.0f;
inline constexpr float minReleaseMs      = 5.0f;
inline constexpr float maxReleaseMs      = 2000.0f;
inline constexpr float defaultReleaseMs  = 80.0f;

/** A ratio parameter's value as a ratio: infinite from halfway to the
    infinite notch on. */
inline float ratioFromParameter (float v) noexcept
{
    return v >= infiniteRatio - 0.5f ? std::numeric_limits<float>::infinity() : juce::jmax (1.0f, v);
}

/** Whether a ceiling ratio parameter is at its cut position. */
inline bool isCeilingCut (float v) noexcept { return v >= cutRatio - 0.5f; }

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
/** Where a degrade stage sits in its region: on the band before the delay
    (the region's Dry hears it too), inside the delay loop (on everything
    written into the lines: the first echo, and again every repeat), or on
    the delay's output. */
enum class DegradePosition { before = 0, loop, after };
inline const juce::StringArray degradePositionNames { "Before", "In loop", "After" };

/** The Transpose stage (time-domain pitch shift), in semitones. */
inline constexpr float maxTransposeSemitones     = 24.0f;
inline constexpr float defaultTransposeSemitones = 12.0f;

/** The Freq shift stage (every frequency moved by the same number of Hz),
    -2 to +2 kHz. The knob is cubic, so the slow, phasing shifts of a few Hz
    near the middle get most of its travel. */
inline constexpr float maxFreqShiftHz     = 2000.0f;
inline constexpr float defaultFreqShiftHz = 30.0f;

inline const juce::StringArray saturationModelNames { "Standard", "Dynamic", "Triode", "Class AB" };

/** Spectral transforms. Pitch in semitones, fractional; blur as the time
    constant of the magnitude smoothing. */
inline constexpr float maxPitchSemitones = 24.0f;
inline constexpr float minBlurMs = 20.0f;
inline constexpr float maxBlurMs = 5000.0f;
inline constexpr float defaultBlurMs = 300.0f;

/** Order of the freeze mode choice; matches fxme::SpectralBandEffects::FreezeMode. */
inline const juce::StringArray freezeModeNames { "Tonal", "Wash" };

/** What drives a region's freeze while its Freeze switch is on. Manual: it
    is simply frozen. Retrigger: it captures again at every step of the
    freeze rate. On/off: frozen for one step, live for the next (frozen on
    the even steps, counted from the host's song position). Stored as a
    choice index: the order is part of the saved state. */
enum class FreezeTrigger { manual = 0, retrigger, onOff };
inline const juce::StringArray freezeTriggerNames { "Manual", "Retrigger", "On/off" };

/** The freeze rate, tempo-synced: 1/64 to 1/2, each as a triplet, straight
    and dotted, in that order. */
inline const juce::StringArray freezeRateNames {
    "1/64 T", "1/64", "1/64 D", "1/32 T", "1/32", "1/32 D", "1/16 T", "1/16", "1/16 D",
    "1/8 T",  "1/8",  "1/8 D",  "1/4 T",  "1/4",  "1/4 D",  "1/2 T",  "1/2",  "1/2 D" };
inline constexpr int defaultFreezeRateIndex = 10;   // 1/8

/** Length of one step of freeze rate `index`, in quarter notes. */
inline double freezeRateQuarters (int index) noexcept
{
    index = juce::jlimit (0, freezeRateNames.size() - 1, index);
    const double straight = 4.0 / (double) (64 >> (index / 3));
    constexpr double factors[] { 2.0 / 3.0, 1.0, 1.5 };
    return straight * factors[index % 3];
}

inline constexpr float minDelayValue = 0.001f;
inline constexpr float maxDelayValue = 3.0f;

/** A new region's delay: a slight echo synced to the host tempo, with a
    little reverb, so the first region drawn already sounds like something
    (its own band at 0 dB, the repeats and the send under it). Times in
    DAW-sync mode, as fractions of a whole note. */
inline constexpr float defaultTimeL         = 0.1875f;   // dotted 1/16
inline constexpr float defaultTimeR         = 0.375f;    // dotted 1/8
inline constexpr float defaultFeedbackDb    = -14.0f;
inline constexpr float defaultDelayLevelDb  = -12.0f;
inline constexpr float defaultReverbSendDb  = -18.0f;

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

/** The `bands` choice. Stereo: each region keeps the input's left and right,
    every decision (gates, ceilings, the spectral effects' analysis) taken on
    their sum; about twice the splitter's CPU. Mono: the input is summed
    first, as the regions always were. */
inline const juce::StringArray bandModeNames { "Stereo", "Mono" };
inline constexpr int monoBandsIndex = 1;

/** The `panlaw` choice, for stereo bands (a mono band is always panned,
    constant power). Balance: the side the region moves away from is turned
    down. Stereo pan: that side is folded into the other, so a region panned
    hard to one side still carries both channels' content there. */
inline const juce::StringArray panLawNames { "Balance", "Stereo pan" };
inline constexpr int stereoPanIndex = 1;

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
