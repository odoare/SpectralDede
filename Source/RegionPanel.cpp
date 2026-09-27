/*
  ------------------------------------------------------------------------------
    RegionPanel.cpp

    See RegionPanel.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "RegionPanel.h"

using namespace dede;

namespace
{
    constexpr int titleHeight = 20;
    constexpr int knobHeight  = 82;
    constexpr int footerHeight = 24;
    constexpr int knobWidth   = 74;
    constexpr int numKnobs    = 7;
    constexpr int panelMargin = 8;
}

RegionPanel::RegionPanel (DedeAudioProcessor& p, int regionSlot)
    : processor (p), slot (regionSlot), accent (theme::regionColour (regionSlot))
{
    timeL = makeKnob (ids::rgTimeL, "Time L");
    timeR = makeKnob (ids::rgTimeR, "Time R");
    fbL   = makeKnob (ids::rgFbL,   "Fb L");
    fbR   = makeKnob (ids::rgFbR,   "Fb R");
    fbX   = makeKnob (ids::rgFbX,   "Cross");
    delayLevel = makeKnob (ids::rgDly, "Delay");
    send  = makeKnob (ids::rgSend,  "Reverb");

    modeBox.addItem ("Seconds",   1);
    modeBox.addItem ("DAW sync",  2);
    modeBox.addItem ("MIDI note", 3);
    theme::styleCombo (modeBox, accent);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), ids::region (slot, ids::rgMode), modeBox);

    setSize (numKnobs * knobWidth + 2 * panelMargin,
             titleHeight + knobHeight + footerHeight + 2 * panelMargin);

    timeText = resolvedTimeText();
    startTimerHz (10);
}

std::unique_ptr<fxme::FxmeSlider> RegionPanel::makeKnob (const char* suffix,
                                                         const juce::String& label)
{
    auto knob = std::make_unique<fxme::FxmeSlider> (processor.getAPVTS(),
                                                    ids::region (slot, suffix),
                                                    label, accent);
    knob->setSliderStyle (juce::Slider::RotaryVerticalDrag);
    knob->setShowLabel (true);
    theme::styleKnob (*knob, accent);
    addAndMakeVisible (*knob);
    return knob;
}

//==============================================================================
juce::String RegionPanel::resolvedTimeText() const
{
    auto& apvts = processor.getAPVTS();
    const auto value = [&apvts, this] (const char* suffix)
    {
        auto* p = apvts.getRawParameterValue (ids::region (slot, suffix));
        return p != nullptr ? p->load() : 0.0f;
    };

    fxme::DelayTimeResolver resolver;
    resolver.bpm        = processor.getCurrentBpm();
    resolver.noteHz     = processor.getLastNoteHz();
    resolver.maxSeconds = maxDelaySeconds;

    const auto mode = (fxme::DelayTimeMode) juce::jlimit (0, numDelayModes - 1,
                                                          (int) value (ids::rgMode));

    const auto ms = [&resolver, mode] (float v)
    {
        return juce::String (resolver.toSeconds (v, mode) * 1000.0f, 1) + " ms";
    };

    juce::String text = "L " + ms (value (ids::rgTimeL)) + "    R " + ms (value (ids::rgTimeR));

    // The note mode has nothing to lock to until something has been played,
    // and silently reading the value as seconds would look like a bug.
    if (mode == fxme::DelayTimeMode::notePeriod && resolver.noteHz <= 0.0f)
        text += "    (no MIDI note yet)";

    return text;
}

void RegionPanel::timerCallback()
{
    const auto text = resolvedTimeText();
    if (text == timeText)
        return;

    timeText = text;
    repaint();
}

//==============================================================================
void RegionPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (accent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    auto area = getLocalBounds().reduced (panelMargin);

    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    g.drawText ("Region " + juce::String (slot + 1) + " delay",
                area.removeFromTop (titleHeight), juce::Justification::centredLeft);

    g.setColour (theme::dimText);
    g.setFont (11.0f);
    g.drawText (timeText, getLocalBounds().reduced (panelMargin)
                              .removeFromBottom (footerHeight),
                juce::Justification::centredRight);
}

void RegionPanel::resized()
{
    auto area = getLocalBounds().reduced (panelMargin);
    area.removeFromTop (titleHeight);

    auto knobs = area.removeFromTop (knobHeight);
    fxme::FxmeSlider* const row[] { timeL.get(), timeR.get(), fbL.get(),
                                    fbR.get(), fbX.get(), delayLevel.get(), send.get() };

    const int w = knobs.getWidth() / numKnobs;
    for (auto* k : row)
        k->setBounds (knobs.removeFromLeft (w).reduced (3));

    modeBox.setBounds (area.removeFromLeft (110).withSizeKeepingCentre (110, 20));
}
