/*
  ------------------------------------------------------------------------------
    OptionsPanel.h

    The settings that shape how Dede works rather than what a region does,
    shown in a callout from the gear button in the top bar:

      - Engine: the splitter's FFT size, with what it comes to (latency, bin
        width, and the step at which the gates move).
      - Gate: attack, release and knee, shared by every region.

    All of them are ordinary parameters, saved with the session and recalled
    by presets like any other (a preset recalls the sound as heard, the
    latency included).

    Also here: GearButton, the button that opens it.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

//==============================================================================
class OptionsPanel : public juce::Component,
                     private juce::Timer
{
public:
    explicit OptionsPanel (DedeAudioProcessor& processor);
    ~OptionsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;

    /** The two read-out lines under the FFT size, from what the processor
        actually runs (a new size is applied asynchronously). */
    juce::StringArray engineText() const;

    DedeAudioProcessor& processor;

    juce::ComboBox fftSizeBox;
    std::unique_ptr<ComboBoxAttachment> fftSizeAttachment;

    fxme::FxmeSlider attackKnob, releaseKnob, kneeKnob;
    fxme::InfoButton infoButton;

    juce::StringArray readout;
    juce::Rectangle<int> engineHeading, fftLabel, readoutArea, gateHeading, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsPanel)
};

//==============================================================================
/** A toothed wheel with a hole, in the top bar's accent colour. */
class GearButton : public juce::Button
{
public:
    GearButton() : juce::Button ("options") {}

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GearButton)
};
