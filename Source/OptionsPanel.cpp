/*
  ------------------------------------------------------------------------------
    OptionsPanel.cpp

    See OptionsPanel.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "OptionsPanel.h"

using namespace dede;

namespace
{
    constexpr int panelMargin   = 10;
    constexpr int titleHeight   = 20;
    constexpr int headingHeight = 18;
    constexpr int rowHeight     = 24;
    constexpr int readoutLine   = 15;
    constexpr int sectionGap    = 10;
    constexpr int knobWidth     = 74;
    constexpr int knobHeight    = 82;
    constexpr int numGateKnobs  = 3;

    const char* const infoBody =
        "Engine\n"
        "The FFT size is the window of the spectral splitter: the regions, "
        "their gates and ceilings, and the spectral effects all work on it, "
        "and the spectrum view measures with it.\n"
        "  larger: finer frequency resolution (narrower regions, cleaner "
        "gating of close partials), but more latency and more smearing in "
        "time (transients soften, gates react later)\n"
        "  smaller: sharper transients and less latency, coarser regions\n"
        "The latency is one window, reported to the host. A change is applied "
        "at once: expect about one window of silence at the switch. Presets "
        "recall it like any other setting.\n\n"
        "Gate (shared by every region)\n"
        "  Attack: how fast a bin opens once it rises over the gate (or falls "
        "under the ceiling)\n"
        "  Release: how fast it closes again. Longer releases let tails through "
        "and stop the gate from warbling on noisy material.\n"
        "  Knee: at 0 a bin is passed or cut outright; wider, it fades in and "
        "out across that many dB around each line.\n"
        "The gates move once per hop, a quarter of the window (the gate step "
        "shown under the FFT size), so an attack shorter than that is "
        "effectively instant.";

    void setUpKnob (juce::Component& parent, fxme::FxmeSlider& knob)
    {
        knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob.setShowLabel (true);
        theme::styleKnob (knob, theme::accent);
        parent.addAndMakeVisible (knob);
    }
}

//==============================================================================
OptionsPanel::OptionsPanel (DedeAudioProcessor& p)
    : processor (p),
      attackKnob  (p.getAPVTS(), ids::gateAttack,  "Attack",  theme::accent),
      releaseKnob (p.getAPVTS(), ids::gateRelease, "Release", theme::accent),
      kneeKnob    (p.getAPVTS(), ids::gateKnee,    "Knee",    theme::accent)
{
    fftSizeBox.addItemList (fftSizeNames, 1);
    theme::styleCombo (fftSizeBox, theme::accent);
    addAndMakeVisible (fftSizeBox);
    fftSizeAttachment = std::make_unique<ComboBoxAttachment> (processor.getAPVTS(),
                                                              ids::fftSize, fftSizeBox);

    setUpKnob (*this, attackKnob);
    setUpKnob (*this, releaseKnob);
    setUpKnob (*this, kneeKnob);

    infoButton.setColours ({ theme::accent, theme::text, theme::panel, theme::panelLine });
    infoButton.setInfo ("Options", infoBody);
    addAndMakeVisible (infoButton);

    readout = engineText();
    startTimerHz (10);

    const int width  = numGateKnobs * knobWidth + 2 * panelMargin;
    const int height = panelMargin + titleHeight + sectionGap
                     + headingHeight + rowHeight + 2 * readoutLine + sectionGap
                     + headingHeight + knobHeight + panelMargin;
    setSize (width, height);
}

OptionsPanel::~OptionsPanel() = default;

//==============================================================================
juce::StringArray OptionsPanel::engineText() const
{
    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const int size  = 1 << processor.getFftOrder();
    const auto ms   = [sr] (int samples) { return juce::String (1000.0 * samples / sr, 1) + " ms"; };

    return { "Latency " + juce::String (size) + " samples (" + ms (size) + ")",
             "Bins " + juce::String (sr / size, 1) + " Hz wide, gate step " + ms (size / 4) };
}

void OptionsPanel::timerCallback()
{
    const auto text = engineText();
    if (text == readout)
        return;

    readout = text;
    repaint (readoutArea);
}

void OptionsPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (theme::accent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (theme::accent);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    g.drawText ("Options", titleArea, juce::Justification::centredLeft);

    g.setColour (theme::text);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    g.drawText ("Engine", engineHeading, juce::Justification::centredLeft);
    g.drawText ("Gate",   gateHeading,   juce::Justification::centredLeft);

    g.setColour (theme::panelLine);
    for (auto h : { engineHeading, gateHeading })
        g.fillRect (h.withTop (h.getBottom() - 1).withHeight (1).toFloat());

    g.setColour (theme::dimText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("FFT size", fftLabel, juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    auto lines = readoutArea;
    for (const auto& line : readout)
        g.drawText (line, lines.removeFromTop (readoutLine), juce::Justification::centredLeft);
}

void OptionsPanel::resized()
{
    auto area = getLocalBounds().reduced (panelMargin);

    titleArea = area.removeFromTop (titleHeight);
    infoButton.setBounds (titleArea.removeFromRight (titleHeight).reduced (1));
    area.removeFromTop (sectionGap);

    engineHeading = area.removeFromTop (headingHeight);
    auto row = area.removeFromTop (rowHeight);
    fftLabel = row.removeFromLeft (70);
    fftSizeBox.setBounds (row.removeFromLeft (100).withSizeKeepingCentre (100, 20));
    readoutArea = area.removeFromTop (2 * readoutLine);
    area.removeFromTop (sectionGap);

    gateHeading = area.removeFromTop (headingHeight);
    auto knobs = area.removeFromTop (knobHeight);
    for (auto* k : { &attackKnob, &releaseKnob, &kneeKnob })
        k->setBounds (knobs.removeFromLeft (knobWidth).reduced (3));
}

//==============================================================================
void GearButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();

    const float outer = d * 0.5f;          // tooth tips
    const float root  = outer * 0.74f;     // between the teeth
    const float hole  = outer * 0.34f;
    constexpr int teeth = 8;

    // Each tooth: a flat top over half its pitch, flanks down to the root.
    juce::Path gear;
    const float pitch = juce::MathConstants<float>::twoPi / (float) teeth;
    for (int i = 0; i < teeth; ++i)
    {
        const float a = (float) i * pitch;
        const float angles[] { a - 0.30f * pitch, a - 0.18f * pitch, a + 0.18f * pitch, a + 0.30f * pitch };
        const float radii[]  { root, outer, outer, root };

        for (int j = 0; j < 4; ++j)
        {
            const auto pt = c.getPointOnCircumference (radii[j], angles[j]);
            if (i == 0 && j == 0)
                gear.startNewSubPath (pt);
            else
                gear.lineTo (pt);
        }

        // Along the root to the next tooth.
        gear.addCentredArc (c.x, c.y, root, root, 0.0f, a + 0.30f * pitch, a + 0.70f * pitch);
    }
    gear.closeSubPath();
    gear.addEllipse (juce::Rectangle<float> (2.0f * hole, 2.0f * hole).withCentre (c));
    gear.setUsingNonZeroWinding (false);

    g.setColour (down ? theme::accent.darker (0.3f)
               : over ? theme::accent.brighter (0.3f)
                      : theme::accent);
    g.fillPath (gear);
}
