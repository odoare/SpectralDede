/*
  ------------------------------------------------------------------------------
    Tooltips.h

    Every hover-help string in the plugin, in one place.

    Sub-namespace per pane; `shared` for anything used by more than one. The
    "?" button of the global controls turns them all on and off (see
    AppSettings.h: uiTooltips).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

namespace dede::tips
{

//==============================================================================
/** Top bar and the levels under the region rows (PluginEditor). */
namespace bar
{
    inline constexpr auto global  = "Global controls: reverb, engine, bands, pan law and FFT size. "
                                    "Its \"?\" turns these tips on and off.";

    inline constexpr auto dry     = "Dry: the untouched input, all of it (the regions add on top). "
                                    "Turn it down to hear only the regions.";
    inline constexpr auto delays  = "Delays: the sum of every region's repeats.";
    inline constexpr auto reverb  = "Reverb: the return of the shared reverb (each region sends to it "
                                    "with its own Reverb knob).";
    inline constexpr auto output  = "Output: the final trim.";
    inline constexpr auto meter   = "Output level: RMS bars, peak markers held for a second, red over "
                                    "0 dBFS.";
}

//==============================================================================
/** The region column, one row per slot (RegionColumn). */
namespace column
{
    inline constexpr auto free   = "Free: delete this region; its slot can be drawn again.";
    inline constexpr auto on     = "On: the region is fed by its band. Off: the band stops feeding it "
                                   "and its tails ring out.";
    inline constexpr auto solo   = "Solo: silence every region not soloed (the dry signal is kept).";
    inline constexpr auto rowFmt = "Region %d: click to select it and edit it under the spectrum.";
}

//==============================================================================
/** The global controls (GlobalPanel, the gear's callout). */
namespace global
{
    inline constexpr auto room    = "Reverb room size: from a small room to a large hall.";
    inline constexpr auto damping = "Reverb damping: how fast the highs die away in the tail.";
    inline constexpr auto width   = "Reverb stereo width: 0 mono, 100 % fully wide.";
    inline constexpr auto engine  = "Spectral: per-bin gates and the spectral effects, one FFT window "
                                    "of latency. Live: filter bands, per-band gates, no latency, no "
                                    "spectral effects.";
    inline constexpr auto bands   = "Stereo: each region keeps the input's left and right. Mono: the "
                                    "input is summed first (half the CPU).";
    inline constexpr auto panLaw  = "For stereo bands. Balance: turns down the side the region moves "
                                    "away from. Stereo pan: folds that side into the other.";
    inline constexpr auto fftSize = "The spectral window: larger for finer regions and cleaner gating, "
                                    "smaller for sharper transients and less latency.";
    inline constexpr auto tooltips = "Hover help on the controls: on or off.";
}

//==============================================================================
/** The region dock: Dynamics tab (RegionPanel). */
namespace dynamics
{
    inline constexpr auto gateSwitch =
        "Gate, the lower (dashed) line: under it, a downward expander (a hard gate by default). "
        "Off while its line is at the bottom; switching it off parks it there, switching it on "
        "brings it back.";
    inline constexpr auto ceilingSwitch =
        "Ceiling, the upper (dotted) line: over it, a compressor, a limiter or a cut (the "
        "default). Off while its line is at the top; switching it off parks it there, switching "
        "it on brings it back.";

    inline constexpr auto gateRatio   = "Expansion ratio under the gate line; at its top, a hard gate.";
    inline constexpr auto gateRange   = "How far down the gate goes; at its bottom, to silence.";
    inline constexpr auto gateKnee    = "Soft knee around the gate line (0: hard).";
    inline constexpr auto gateAttack  = "How fast the gate opens when the level comes over the line.";
    inline constexpr auto gateRelease = "How fast the gate closes when the level falls under the line.";

    inline constexpr auto ceilRatio   = "Compression ratio over the ceiling line; then infinity (a "
                                        "limiter), then cut (mute over the line).";
    inline constexpr auto ceilKnee    = "Soft knee around the ceiling line (0: hard).";
    inline constexpr auto ceilAttack  = "How fast the ceiling reacts when the level goes over the line.";
    inline constexpr auto ceilRelease = "How fast it lets go when the level comes back under.";
}

//==============================================================================
/** The region dock: Spectral tab. */
namespace spectral
{
    inline constexpr auto page   = "Pitch, freeze and blur work on the FFT: they have no effect in "
                                   "the Live engine.";

    inline constexpr auto pitch      = "Pitch: shifts the band in semitones, inside the spectral frame "
                                       "(no time stretching).";
    inline constexpr auto semitones  = "Pitch shift in semitones, fractional.";

    inline constexpr auto freeze     = "Freeze: holds the band's spectrum, ignoring the input.";
    inline constexpr auto freezeMode = "Tonal: held notes stay notes. Wash: a smooth, static texture.";
    inline constexpr auto trigger    = "Manual: frozen while Freeze is on. Retrigger: captures again at "
                                       "every step of the rate. On/off: frozen for one step, live for "
                                       "the next.";
    inline constexpr auto rate       = "Step of the Retrigger and On/off triggers, synced to the host "
                                       "tempo (T: triplet, D: dotted).";
    inline constexpr auto width      = "How far the frozen left and right channels drift apart in phase: "
                                       "0 keeps the image, 100 % makes them independent. Stereo bands "
                                       "only.";

    inline constexpr auto blur       = "Blur: smears the band's spectrum over time, a reverb-like tail "
                                       "confined to the band.";
    inline constexpr auto blurTime   = "How long the smear lasts.";
}

//==============================================================================
/** The region dock: Delay tab. */
namespace delay
{
    inline constexpr auto delaySwitch =
        "The region's repeats. Follows the Delay level (in the strip): off at -inf. Switching it "
        "off parks the level there, switching it on brings it back.";
    inline constexpr auto timeL    = "Left delay time, read as the Time mode says.";
    inline constexpr auto timeR    = "Right delay time, read as the Time mode says.";
    inline constexpr auto fbL      = "Left feedback: how much of each repeat comes back.";
    inline constexpr auto fbR      = "Right feedback: how much of each repeat comes back.";
    inline constexpr auto cross    = "Cross feedback: left repeats into the right line and back "
                                     "(ping-pong).";
    inline constexpr auto mode     = "Seconds; DAW sync (a fraction of a whole note: 0.25 is a quarter); "
                                     "or MIDI note (a multiple of the last note's period: a tuned "
                                     "resonator).";
}

//==============================================================================
/** The region dock: Degrade tab. */
namespace degrade
{
    inline constexpr auto saturation = "Saturation: drives the band into a waveshaper, its level kept.";
    inline constexpr auto model      = "Saturation model.";
    inline constexpr auto drive      = "How hard the band is driven.";
    inline constexpr auto crush      = "Bit-crush: fewer bits, more grit.";
    inline constexpr auto bits       = "Bit depth, fractional.";
    inline constexpr auto downsample = "Downsample: holds each sample, lowering the effective sample "
                                       "rate (aliasing included).";
    inline constexpr auto factor     = "How many samples each held value lasts.";
    inline constexpr auto transpose  = "Transpose: a time-domain pitch shift. In the loop, every repeat "
                                       "climbs by the interval.";
    inline constexpr auto semitones  = "Transposition in semitones.";
    inline constexpr auto freqShift  = "Freq shift: moves every frequency by the same number of Hz "
                                       "(inharmonic). In the loop, the repeats spiral up or down.";
    inline constexpr auto hz         = "Shift in Hz, up or down.";
    inline constexpr auto position   = "Where this stage runs: before the delay (the region's Dry hears "
                                       "it too), in the delay loop (the first echo, then again on every "
                                       "repeat), or after the delay (on the repeats).";
}

//==============================================================================
/** The region dock: the output strip and the region preset bar. */
namespace strip
{
    inline constexpr auto dry    = "The region's own band, not delayed (panned, through the stages "
                                   "placed before the delay), on top of the global dry.";
    inline constexpr auto delay  = "The region's repeats.";
    inline constexpr auto reverb = "Send to the shared reverb, taken after Dry and Delay.";
}

} // namespace dede::tips
