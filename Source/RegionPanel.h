/*
  ------------------------------------------------------------------------------
    RegionPanel.h

    The settings of one region's delay, shown in a callout when its rectangle
    is clicked on the spectrum view: the two delay times and how they are read
    (seconds, host tempo, or the last MIDI note), the three feedback gains, and
    the two levels the repeats leave with: into the mix (Delay) and into the
    shared reverb (Reverb), independent of each other.

    Everything a region does to the spectrum itself (its borders, its gate, its
    gain and pan) is set by dragging on the view, so none of it appears here.
    The panel is only what has no natural handle on a spectrum.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

class RegionPanel : public juce::Component,
                    private juce::Timer
{
public:
    RegionPanel (DedeAudioProcessor& processor, int slot);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** What the two delay values currently come to in milliseconds, which is
        the only way to read a sync or note-locked time at a glance. */
    juce::String resolvedTimeText() const;

    using SliderAttachment  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    /** One knob bound to this region's `suffix` parameter. */
    std::unique_ptr<fxme::FxmeSlider> makeKnob (const char* suffix, const juce::String& label);

    DedeAudioProcessor& processor;
    const int slot;
    const juce::Colour accent;

    std::unique_ptr<fxme::FxmeSlider> timeL, timeR, fbL, fbR, fbX, delayLevel, send;
    juce::ComboBox modeBox;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;

    juce::String timeText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionPanel)
};
