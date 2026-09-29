/*
  ------------------------------------------------------------------------------
    OutputMeter.h

    The plugin's output level, at the bottom of the left column: two
    horizontal bars (L, R) showing the RMS level over 100 ms, a peak marker
    that holds for a second then falls, and a dB scale under them. The bars
    and markers turn red over 0 dBFS.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

class OutputMeter : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::Timer
{
public:
    explicit OutputMeter (DedeAudioProcessor& processor);
    ~OutputMeter() override;

    void paint (juce::Graphics&) override;

    /** Height it needs: two bars and the scale. */
    static constexpr int preferredHeight = 34;

private:
    void timerCallback() override;

    juce::Rectangle<float> barArea() const;
    float xFor (float db, juce::Rectangle<float> area) const noexcept;

    DedeAudioProcessor& processor;

    struct Channel
    {
        float rmsDb   = -100.0f;   // as shown: rises at once, falls steadily
        float peakDb  = -100.0f;   // the held peak marker
        int   holdTicks = 0;
    };
    Channel channels[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMeter)
};
