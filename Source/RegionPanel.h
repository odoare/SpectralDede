/*
  ------------------------------------------------------------------------------
    RegionPanel.h

    The settings of one region, shown in a callout when its rectangle is
    clicked on the spectrum view, in tabs:

      - Delay: the two delay times and how they are read (seconds, host tempo,
        or the last MIDI note), the three feedback gains, and the region's
        output: its own band undelayed (Dry, off by default, added on top of
        the global dry), its repeats (Delay), and the send to the shared reverb
        (Reverb), taken after those two levels.
      - Degrade: saturation, bit-crush, downsampling and a time-domain pitch
        shift (Shift), each with its own switch and its own position: before
        the delay (on the mono band, so the region's Dry hears it too), in
        the delay loop (on what enters the lines: the first echo, then again
        on every repeat), or after the delay (on the repeats).
      - Spectral: pitch shift (fractional semitones), freeze (tonal or wash)
        and blur, each with its own switch, done inside the splitter's frame
        before the band leaves it (so before the degrade chain).

    Everything a region does to the spectrum itself (its borders, its gate and
    ceiling, its gain and pan) is set by dragging on the view, so none of it
    appears here. The panel is only what has no natural handle on a spectrum.

    The tab last shown is remembered by the editor (not saved), so reopening a
    region, or opening another, lands where the user was.

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
    /** `lastTab` is owned by the editor, which outlives the callout holding
        this panel: it is read to pick the opening tab and written whenever the
        user switches. */
    RegionPanel (DedeAudioProcessor& processor, int slot, int& lastTab);
    ~RegionPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DelayPage;
    class StagesPage;
    class Tabs;

    const int slot;
    const juce::Colour accent;
    std::unique_ptr<Tabs> tabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionPanel)
};
