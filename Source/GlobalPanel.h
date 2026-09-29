/*
  ------------------------------------------------------------------------------
    GlobalPanel.h

    The global controls that are set once rather than played, shown in a
    callout from the gear button in the top bar:

      - Reverb: room, damping and width of the shared reverb (its return
        level is with the other levels, in the left column).
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
class GlobalPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit GlobalPanel (DedeAudioProcessor& processor);
    ~GlobalPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;

    /** The two read-out lines under the FFT size, from what the processor
        actually runs (a new size is applied asynchronously). */
    juce::StringArray engineText() const;

    DedeAudioProcessor& processor;

    fxme::FxmeSlider roomKnob, dampKnob, widthKnob;

    juce::ComboBox fftSizeBox;
    std::unique_ptr<ComboBoxAttachment> fftSizeAttachment;

    fxme::FxmeSlider attackKnob, releaseKnob, kneeKnob;
    fxme::InfoButton infoButton;

    juce::StringArray readout;
    juce::Rectangle<int> titleArea, reverbHeading, engineHeading, fftLabel, readoutArea, gateHeading;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalPanel)
};

//==============================================================================
/** A toothed wheel with a hole, in the top bar's accent colour. */
class GearButton : public juce::Button
{
public:
    GearButton() : juce::Button ("global") {}

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GearButton)
};
