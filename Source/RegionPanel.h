/*
  ------------------------------------------------------------------------------
    RegionPanel.h

    The settings of the selected region, docked under the spectrum view (so
    its lines and handles can be dragged while its knobs are in sight): tabs
    in signal order on the left, the region's output strip on the right.

    The tabs:

      - Dynamics: what happens around the region's two lines (dragged on the
        view): under the gate a downward expander (ratio, range; a hard gate
        by default), over the ceiling a compressor (ratio; a limiter at its
        top) or a cut (the default), each with its knee, attack and release.
        Per frequency bin in the Spectral engine (a spectral expander /
        compressor), on the band's level in the Live engine (a multiband
        one).
      - Spectral: pitch shift (fractional semitones), freeze (tonal or wash;
        manual, retriggered or gated on/off at a synced rate; stereo width)
        and blur, each with its own switch, done inside the splitter's frame
        before the band leaves it (so before the delay and degrade stages).
      - Delay: the two delay times and how they are read (seconds, host tempo,
        or the last MIDI note), with what they come to in milliseconds, and
        the three feedback gains.
      - Degrade: saturation, bit-crush, downsampling, Transpose (a time-domain
        pitch shift, in semitones) and Freq shift (every frequency moved by
        the same number of Hz), each with its own switch and its own position,
        three small radio buttons left of its knobs: before the delay (on the mono band, so the region's Dry hears it too), in
        the delay loop (on what enters the lines: the first echo, then again
        on every repeat), or after the delay (on the repeats).

    The strip, always in view whatever the tab: the region's own band
    undelayed (Dry, 0 dB by default, added on top of the global dry), its
    repeats (Delay), and the send to the shared reverb (Reverb), taken after
    those two levels. Its gain and pan are the line and the round handle on
    the view.

    The region's borders and the levels of its gate and ceiling lines are set
    by dragging on the view only.

    The Dynamics tab's Gate and Ceiling switches and the Delay tab's switch
    have no parameter of their own: they follow the gate line, the ceiling
    line and the strip's Delay level, off at the end of their range, and
    switching one off parks its value there (switching on brings it back).

    The editor makes a new panel whenever the selection changes; the tab last
    shown is remembered by the editor (not saved), so selecting another
    region lands where the user was.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

class RegionPanel : public juce::Component
{
public:
    /** What the dock remembers from one region panel to the next, for the
        editor's life (not saved): the tab last shown, and where each region's
        gate, ceiling and Delay level were when switched off, for switching
        them back on (a line never dragged comes back at -60 or -12 dB, the
        Delay level at 0 dB). */
    struct Session
    {
        Session();

        int lastTab = 0;
        float gateDb[dede::maxRegions], ceilingDb[dede::maxRegions], delayDb[dede::maxRegions];
    };

    /** `session` is owned by the editor, which owns this panel. */
    RegionPanel (DedeAudioProcessor& processor, int slot, Session& session);
    ~RegionPanel() override;

    /** The panel's height, fixed, and the least width its knobs fit in. */
    static int preferredHeight();
    static int minimumWidth();

    int getSlot() const noexcept { return slot; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DynamicsPage;
    class DelayPage;
    class StagesPage;
    class Tabs;
    class Strip;

    /** Where the strip sits, its heading included. */
    juce::Rectangle<int> stripArea() const;

    const int slot;
    const juce::Colour accent;
    std::unique_ptr<Tabs> tabs;
    std::unique_ptr<Strip> strip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionPanel)
};
