/*
  ------------------------------------------------------------------------------
    PluginEditor.cpp

    See PluginEditor.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PluginEditor.h"

using namespace dede;

namespace
{
    const char* const helpBody =
        "Dede splits the incoming signal into frequency regions and gives each "
        "one its own stereo delay, plus a send to a shared reverb.\n\n"
        "The spectrum view is where regions are drawn. Drag on an empty part of "
        "it to create a region, drag its borders to move them, drag the "
        "horizontal segment to set the gate (only what rises above it passes), "
        "and drag the round handle to set the region's gain (up/down) and pan "
        "(left/right). Click a region to open its delay settings.\n\n"
        "The bottom row holds the levels of the dry signal, the summed delays "
        "and the reverb return, the output trim, and the reverb itself.";
}

//==============================================================================
DedeAudioProcessorEditor::DedeAudioProcessorEditor (DedeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    topBar.setAccentColour (theme::accent);
    topBar.setBackgroundColour (theme::topBarBg);
    addAndMakeVisible (topBar);

    presetBar.setAccentColour (theme::accent);
    infoButton.setColours ({ theme::accent, theme::text, theme::panel, theme::panelLine });
    infoButton.setInfo ("Dede", helpBody);
    topBar.setRightControls (&presetBar, 230, &infoButton, 22);

    fxme::SpectrumDisplay::Colours specColours;
    specColours.plotBackground = theme::plotBack;
    specColours.grid           = theme::grid;
    specColours.gridZero       = theme::gridZero;
    specColours.text           = theme::text;
    specColours.dimText        = theme::dimText;
    specColours.panelLine      = theme::panelLine;
    spectrum.setColours (specColours);
    spectrum.setDbRange (-100.0f, 0.0f);
    spectrum.sampleRateProvider = [this] { return processor.getSampleRate(); };
    spectrum.addTrace ({ &processor.getInputTap(), theme::trace,
                         [] { return juce::String ("input"); }, 1.4f });
    addAndMakeVisible (spectrum);

    setUpKnob (dryKnob,      theme::accent);
    setUpKnob (wetKnob,      theme::accent);
    setUpKnob (revLevelKnob, theme::specViolet);
    setUpKnob (outKnob,      theme::accent);
    setUpKnob (roomKnob,     theme::specViolet);
    setUpKnob (dampKnob,     theme::specViolet);
    setUpKnob (widthKnob,    theme::specViolet);

    processor.getPresetManager().addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (760, 480, 2400, 1600);
    setSize (theme::defaultWidth, theme::defaultHeight);
}

DedeAudioProcessorEditor::~DedeAudioProcessorEditor()
{
    processor.getPresetManager().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DedeAudioProcessorEditor::setUpKnob (fxme::FxmeSlider& knob, juce::Colour accent)
{
    knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    knob.setShowLabel (true);
    theme::styleKnob (knob, accent);
    addAndMakeVisible (knob);
}

//==============================================================================
void DedeAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Loading a preset can move every region, so anything derived from the
    // parameters has to be rebuilt rather than merely repainted. For now the
    // spectrum view holds no derived state, so a repaint is enough; the region
    // editor added in step 3 hooks in here.
    repaint();
}

void DedeAudioProcessorEditor::paint (juce::Graphics& g)
{
    theme::paintBackground (g, getLocalBounds().toFloat());
}

void DedeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (theme::topBarHeight));

    area.reduce (theme::margin, theme::margin);

    auto controls = area.removeFromBottom (theme::globalRowHeight);
    area.removeFromBottom (theme::margin);
    spectrum.setBounds (area);

    // Four level knobs on the left, the three reverb knobs on the right, with
    // whatever is left over as a gap between the two groups.
    fxme::FxmeSlider* const levels[] { &dryKnob, &wetKnob, &revLevelKnob, &outKnob };
    fxme::FxmeSlider* const reverb[] { &roomKnob, &dampKnob, &widthKnob };

    const int knobW = juce::jmin (86, controls.getWidth() / 8);

    auto leftArea = controls.removeFromLeft (knobW * (int) std::size (levels));
    for (auto* k : levels)
        k->setBounds (leftArea.removeFromLeft (knobW).reduced (4));

    auto rightArea = controls.removeFromRight (knobW * (int) std::size (reverb));
    for (auto* k : reverb)
        k->setBounds (rightArea.removeFromLeft (knobW).reduced (4));
}
