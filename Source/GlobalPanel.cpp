/*
  ------------------------------------------------------------------------------
    GlobalPanel.cpp

    See GlobalPanel.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "GlobalPanel.h"

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
    constexpr int knobsPerRow   = 3;

    const char* const infoBody =
        "Reverb\n"
        "One reverb shared by every region, fed by each region's Reverb send. "
        "Room is its size and decay, Damping how fast the highs die away, "
        "Width its stereo spread. Its return level is the Reverb knob in the "
        "left column.\n\n"
        "Engine\n"
        "Spectral (the default) cuts the regions out of a short-time Fourier "
        "transform: near brick-wall edges, gates that decide bin by bin (a "
        "region can keep only its peaks, or only its floor), and the spectral "
        "effects. It costs one FFT window of latency.\n"
        "Live cuts them with filters (24 dB per octave each side) instead: no "
        "latency, sharp transients, less CPU, for playing live. The gate and "
        "ceiling then open and close the whole band, on its overall level, "
        "shown as a shaded column in each region; neighbouring regions "
        "overlap near their edges, narrow ones lose a little level, a region's "
        "Dry colours the sound a little near its edges, and the Spectral tab "
        "(pitch, freeze, blur) has no effect. Switching resets every region "
        "(delay tails included) and changes the latency.\n\n"
        "Bands: Stereo keeps each region's left and right, as they come in; "
        "every decision (gates, ceilings, the spectral effects' analysis) is "
        "still taken on their sum, the signal the spectrum shows, and applied "
        "to both sides alike, so a region keeps its image. It costs about "
        "twice the CPU of Mono, which sums the input first (as regions always "
        "did) and pans each region as a mono source. A centred region in "
        "Stereo comes out 3 dB louder than in Mono (it is not panned to the "
        "middle, it stays both sides at full level). Switching resets every "
        "region.\n"
        "Pan law (stereo bands): Balance turns down the side a region moves "
        "away from; Stereo pan folds that side into the other, so a region "
        "panned hard keeps both channels' content on one side.\n\n"
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
        "In the Spectral engine the gates move once per hop, a quarter of the "
        "window (the gate step shown under the FFT size), so an attack shorter "
        "than that is effectively instant. In the Live engine they move every "
        "sample, so every attack time is meaningful.";

    void setUpKnob (juce::Component& parent, fxme::FxmeSlider& knob, juce::Colour accent)
    {
        knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob.setShowLabel (true);
        theme::styleKnob (knob, accent);
        parent.addAndMakeVisible (knob);
    }

    void paintHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (theme::text);
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        g.drawText (text, area, juce::Justification::centredLeft);
        g.setColour (theme::panelLine);
        g.fillRect (area.withTop (area.getBottom() - 1).withHeight (1).toFloat());
    }
}

//==============================================================================
GlobalPanel::GlobalPanel (DedeAudioProcessor& p)
    : processor (p),
      roomKnob    (p.getAPVTS(), ids::revRoom,     "Room",    theme::specViolet),
      dampKnob    (p.getAPVTS(), ids::revDamp,     "Damping", theme::specViolet),
      widthKnob   (p.getAPVTS(), ids::revWidth,    "Width",   theme::specViolet),
      attackKnob  (p.getAPVTS(), ids::gateAttack,  "Attack",  theme::accent),
      releaseKnob (p.getAPVTS(), ids::gateRelease, "Release", theme::accent),
      kneeKnob    (p.getAPVTS(), ids::gateKnee,    "Knee",    theme::accent)
{
    for (auto* k : { &roomKnob, &dampKnob, &widthKnob })
        setUpKnob (*this, *k, theme::specViolet);

    engineBox.addItemList (engineNames, 1);
    theme::styleCombo (engineBox, theme::accent);
    addAndMakeVisible (engineBox);
    engineAttachment = std::make_unique<ComboBoxAttachment> (processor.getAPVTS(),
                                                             ids::engine, engineBox);

    bandsBox.addItemList (bandModeNames, 1);
    theme::styleCombo (bandsBox, theme::accent);
    addAndMakeVisible (bandsBox);
    bandsAttachment = std::make_unique<ComboBoxAttachment> (processor.getAPVTS(),
                                                            ids::bandMode, bandsBox);

    panLawBox.addItemList (panLawNames, 1);
    theme::styleCombo (panLawBox, theme::accent);
    addAndMakeVisible (panLawBox);
    panLawAttachment = std::make_unique<ComboBoxAttachment> (processor.getAPVTS(),
                                                             ids::panLaw, panLawBox);

    fftSizeBox.addItemList (fftSizeNames, 1);
    theme::styleCombo (fftSizeBox, theme::accent);
    addAndMakeVisible (fftSizeBox);
    fftSizeAttachment = std::make_unique<ComboBoxAttachment> (processor.getAPVTS(),
                                                              ids::fftSize, fftSizeBox);

    for (auto* k : { &attackKnob, &releaseKnob, &kneeKnob })
        setUpKnob (*this, *k, theme::accent);

    infoButton.setColours ({ theme::accent, theme::text, theme::panel, theme::panelLine });
    infoButton.setInfo ("Global controls", infoBody);
    addAndMakeVisible (infoButton);

    readout = engineText();
    timerCallback();
    startTimerHz (10);

    const int width  = knobsPerRow * knobWidth + 2 * panelMargin;
    const int height = panelMargin + titleHeight + sectionGap
                     + headingHeight + knobHeight + sectionGap
                     + headingHeight + 4 * rowHeight + 2 * readoutLine + sectionGap
                     + headingHeight + knobHeight + panelMargin;
    setSize (width, height);
}

GlobalPanel::~GlobalPanel() = default;

//==============================================================================
juce::StringArray GlobalPanel::engineText() const
{
    if (processor.isLiveEngine())
        return { "No latency; gates act on each whole band",
                 "Soft edges, no spectral effects" };

    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const int size  = 1 << processor.getFftOrder();
    const auto ms   = [sr] (int samples) { return juce::String (1000.0 * samples / sr, 1) + " ms"; };

    return { "Latency " + juce::String (size) + " samples (" + ms (size) + ")",
             "Bins " + juce::String (sr / size, 1) + " Hz wide, gate step " + ms (size / 4) };
}

void GlobalPanel::timerCallback()
{
    // The FFT size only means something to the spectral engine, the pan law
    // only to stereo bands.
    const auto enableRow = [this] (juce::ComboBox& box, juce::Rectangle<int> label, bool on)
    {
        if (box.isEnabled() == on)
            return;
        box.setEnabled (on);
        box.setAlpha (on ? 1.0f : 0.45f);
        repaint (label);
    };
    enableRow (fftSizeBox, fftLabel, ! processor.isLiveEngine());
    enableRow (panLawBox, panLawLabel, processor.areBandsStereo());

    const auto text = engineText();
    if (text == readout)
        return;

    readout = text;
    repaint (readoutArea);
}

void GlobalPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (theme::accent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (theme::accent);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    g.drawText ("Global controls", titleArea, juce::Justification::centredLeft);

    paintHeading (g, "Reverb", reverbHeading);
    paintHeading (g, "Engine", engineHeading);
    paintHeading (g, "Gate",   gateHeading);

    g.setColour (theme::dimText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Engine", engineLabel, juce::Justification::centredLeft);
    g.drawText ("Bands",  bandsLabel,  juce::Justification::centredLeft);
    g.setColour (theme::dimText.withAlpha (panLawBox.isEnabled() ? 1.0f : 0.45f));
    g.drawText ("Pan law", panLawLabel, juce::Justification::centredLeft);
    g.setColour (theme::dimText.withAlpha (fftSizeBox.isEnabled() ? 1.0f : 0.45f));
    g.drawText ("FFT size", fftLabel, juce::Justification::centredLeft);
    g.setColour (theme::dimText);

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    auto lines = readoutArea;
    for (const auto& line : readout)
        g.drawText (line, lines.removeFromTop (readoutLine), juce::Justification::centredLeft);
}

void GlobalPanel::resized()
{
    auto area = getLocalBounds().reduced (panelMargin);

    titleArea = area.removeFromTop (titleHeight);
    infoButton.setBounds (titleArea.removeFromRight (titleHeight).reduced (1));
    area.removeFromTop (sectionGap);

    const auto knobRow = [&area] (std::initializer_list<fxme::FxmeSlider*> knobs)
    {
        auto row = area.removeFromTop (knobHeight);
        for (auto* k : knobs)
            k->setBounds (row.removeFromLeft (knobWidth).reduced (3));
    };

    reverbHeading = area.removeFromTop (headingHeight);
    knobRow ({ &roomKnob, &dampKnob, &widthKnob });
    area.removeFromTop (sectionGap);

    engineHeading = area.removeFromTop (headingHeight);
    const auto comboRow = [&area] (juce::Rectangle<int>& label, juce::ComboBox& box)
    {
        auto row = area.removeFromTop (rowHeight);
        label = row.removeFromLeft (70);
        box.setBounds (row.removeFromLeft (110).withSizeKeepingCentre (110, 20));
    };
    comboRow (engineLabel, engineBox);
    comboRow (bandsLabel,  bandsBox);
    comboRow (panLawLabel, panLawBox);
    comboRow (fftLabel,    fftSizeBox);
    readoutArea = area.removeFromTop (2 * readoutLine);
    area.removeFromTop (sectionGap);

    gateHeading = area.removeFromTop (headingHeight);
    knobRow ({ &attackKnob, &releaseKnob, &kneeKnob });
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
