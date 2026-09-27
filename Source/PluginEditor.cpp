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
#include "RegionPanel.h"

using namespace dede;

namespace
{
    const char* const helpBody =
        "Dede splits the incoming signal into frequency regions and gives each "
        "one its own stereo delay, plus a send to a shared reverb.\n\n"
        "Drawing regions:\n"
        "  drag on empty space to draw a new one\n"
        "  drag a border to move it, drag inside to move the whole band\n"
        "  drag the dashed line to set the gate: what rises above it passes\n"
        "  drag the dotted line to set the ceiling: what rises above it is cut\n"
        "    (at the top it is off; the two lines push each other apart)\n"
        "  drag the round handle for gain (up/down) and pan (left/right)\n"
        "  click a region to open its settings: delay, and degrade (saturation,\n"
        "    bit-crush, downsample, applied to the band before its delay)\n"
        "  a region's Dry adds its own band, degraded and panned but not delayed,\n"
        "    on top of the global dry; its reverb send takes Dry and Delay after\n"
        "    their levels\n"
        "  Delete or Backspace removes the selected region\n\n"
        "Delay times read three ways, chosen per region: seconds, a proportion "
        "of a whole note at the host tempo (0.25 is a quarter note), or a "
        "multiple of the period of the last MIDI note received. The panel shows "
        "what each side currently comes to in milliseconds.\n\n"
        "Spectrum view:\n"
        "  wheel zooms the level axis, ctrl+wheel the frequency axis\n"
        "  alt+drag or middle-drag pans, double-click resets\n"
        "  the window size is pinned to the one the gates are measured in\n\n"
        "The bottom row holds the levels of the dry signal, the summed delays "
        "and the reverb return, the output trim, the gate knee, and the reverb "
        "itself. The knee (shared by every region) softens the gate and the "
        "ceiling: at 0 a bin is passed or cut outright, wider it fades in and "
        "out across that many dB around each line.";
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
    presetBrowserButton.setTooltip ("Browse, save and delete presets");
    presetBrowserButton.setColour (juce::TextButton::textColourOffId, theme::accent);
    presetBrowserButton.onClick = [this] { showPresetBrowser(); };

    infoButton.setColours ({ theme::accent, theme::text, theme::panel, theme::panelLine });
    infoButton.setInfo ("Dede", helpBody);
    topBar.setRightControls ({ { &presetBar, 210 }, { &presetBrowserButton, 26 },
                               { &infoButton, 22 } });

    fxme::SpectrumDisplay::Colours specColours;
    specColours.plotBackground = theme::plotBack;
    specColours.grid           = theme::grid;
    specColours.gridZero       = theme::gridZero;
    specColours.text           = theme::text;
    specColours.dimText        = theme::dimText;
    specColours.panelLine      = theme::panelLine;
    spectrum.setColours (specColours);
    spectrum.setDbRange (gateFloorDb, gateCeilDb);
    spectrum.sampleRateProvider = [this] { return processor.getSampleRate(); };
    spectrum.addTrace ({ &processor.getInputTap(), theme::trace,
                         [] { return juce::String ("input"); }, 1.4f });

    // The gate thresholds are per-bin levels measured in the splitter's own
    // analysis window, so the view is pinned to that same window: otherwise a
    // line drawn on the trace would not be the level the DSP compares against.
    spectrum.setFftOrder (splitterFftOrder);
    spectrum.setFftSizeLocked (true);

    spectrum.setNumRegions (maxRegions);
    spectrum.setGainRange (minGainDb, maxGainDb);
    spectrum.setGateRange (gateFloorDb, gateCeilDb);
    spectrum.setCeilingRange (gateFloorDb, gateCeilDb);
    spectrum.setCeilingEnabled (true);
    wireRegionEditor();
    refreshRegionsFromParameters (true);
    addAndMakeVisible (spectrum);

    setUpKnob (dryKnob,      theme::accent);
    setUpKnob (wetKnob,      theme::accent);
    setUpKnob (revLevelKnob, theme::specViolet);
    setUpKnob (outKnob,      theme::accent);
    setUpKnob (kneeKnob,     theme::accent);
    setUpKnob (roomKnob,     theme::specViolet);
    setUpKnob (dampKnob,     theme::specViolet);
    setUpKnob (widthKnob,    theme::specViolet);

    processor.getPresetManager().addChangeListener (this);
    startTimerHz (20);

    setResizable (true, true);
    setResizeLimits (760, 480, 2400, 1600);
    setSize (theme::defaultWidth, theme::defaultHeight);
}

DedeAudioProcessorEditor::~DedeAudioProcessorEditor()
{
    stopTimer();
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
void DedeAudioProcessorEditor::setParameter (const juce::String& id, float value)
{
    if (auto* p = processor.getAPVTS().getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

int DedeAudioProcessorEditor::findFreeSlot() const
{
    for (int i = 0; i < maxRegions; ++i)
        if (auto* p = processor.getAPVTS().getRawParameterValue (ids::region (i, ids::rgOn)))
            if (p->load() <= 0.5f)
                return i;

    return -1;
}

fxme::SpectrumRegionEditor::Region
DedeAudioProcessorEditor::regionFromParameters (int slot) const
{
    auto& apvts = processor.getAPVTS();
    const auto value = [&apvts, slot] (const char* suffix)
    {
        auto* p = apvts.getRawParameterValue (ids::region (slot, suffix));
        return p != nullptr ? p->load() : 0.0f;
    };

    const float a = value (ids::rgFLo), b = value (ids::rgFHi);

    fxme::SpectrumRegionEditor::Region r;
    r.active = value (ids::rgOn) > 0.5f;
    r.lowHz  = juce::jmin (a, b);
    r.highHz = juce::jmax (a, b);
    r.gateDb = value (ids::rgGate);
    r.ceilingDb = value (ids::rgCeil);
    r.gainDb = value (ids::rgGain);
    r.pan    = value (ids::rgPan);
    r.colour = theme::regionColour (slot);
    r.label  = juce::String (slot + 1);
    return r;
}

void DedeAudioProcessorEditor::refreshRegionsFromParameters (bool force)
{
    for (int i = 0; i < maxRegions; ++i)
    {
        const auto r = regionFromParameters (i);
        const auto& shown = shownRegions[i];

        const auto same = [] (float a, float b) { return juce::approximatelyEqual (a, b); };

        const bool changed = force
            || r.active != shown.active
            || ! same (r.lowHz,  shown.lowHz)
            || ! same (r.highHz, shown.highHz)
            || ! same (r.gateDb, shown.gateDb)
            || ! same (r.ceilingDb, shown.ceilingDb)
            || ! same (r.gainDb, shown.gainDb)
            || ! same (r.pan,    shown.pan);

        if (! changed)
            continue;

        shownRegions[i] = r;
        spectrum.setRegion (i, r);
    }
}

void DedeAudioProcessorEditor::beginGesture (int slot,
                                             fxme::SpectrumRegionEditor::Handle handle,
                                             bool begin)
{
    using Handle = fxme::SpectrumRegionEditor::Handle;

    // The parameters a handle writes to, so a drag is bracketed as one host
    // automation gesture on exactly those.
    std::vector<const char*> touched;
    switch (handle)
    {
        case Handle::leftEdge:  touched = { ids::rgFLo }; break;
        case Handle::rightEdge: touched = { ids::rgFHi }; break;
        // Either level line can push the other along, so a drag of one is a
        // gesture on both.
        case Handle::gate:
        case Handle::ceiling:   touched = { ids::rgGate, ids::rgCeil }; break;
        case Handle::gainPan:   touched = { ids::rgGain, ids::rgPan }; break;
        case Handle::body:      touched = { ids::rgFLo, ids::rgFHi }; break;
        case Handle::none:
        default:                return;
    }

    for (auto* suffix : touched)
        if (auto* p = processor.getAPVTS().getParameter (ids::region (slot, suffix)))
            begin ? p->beginChangeGesture() : p->endChangeGesture();
}

void DedeAudioProcessorEditor::wireRegionEditor()
{
    using Region = fxme::SpectrumRegionEditor::Region;
    using Handle = fxme::SpectrumRegionEditor::Handle;

    spectrum.onRegionChanged = [this] (int slot, const Region& r)
    {
        setParameter (ids::region (slot, ids::rgFLo),  r.lowHz);
        setParameter (ids::region (slot, ids::rgFHi),  r.highHz);
        setParameter (ids::region (slot, ids::rgGate), r.gateDb);
        setParameter (ids::region (slot, ids::rgCeil), r.ceilingDb);
        setParameter (ids::region (slot, ids::rgGain), r.gainDb);
        setParameter (ids::region (slot, ids::rgPan),  r.pan);
        shownRegions[slot] = r;
    };

    spectrum.onRegionCreate = [this] (float lowHz, float highHz) -> int
    {
        const int slot = findFreeSlot();
        if (slot < 0)
            return -1;

        // A new region starts wide open and centred: the band the user drew is
        // the only thing they have expressed an opinion about.
        setParameter (ids::region (slot, ids::rgFLo),  lowHz);
        setParameter (ids::region (slot, ids::rgFHi),  highHz);
        setParameter (ids::region (slot, ids::rgGate), gateFloorDb);
        setParameter (ids::region (slot, ids::rgCeil), gateCeilDb);
        setParameter (ids::region (slot, ids::rgGain), 0.0f);
        setParameter (ids::region (slot, ids::rgPan),  0.0f);
        setParameter (ids::region (slot, ids::rgOn),   1.0f);

        refreshRegionsFromParameters (true);
        return slot;
    };

    spectrum.onRegionDelete = [this] (int slot)
    {
        setParameter (ids::region (slot, ids::rgOn), 0.0f);
        refreshRegionsFromParameters (true);
    };

    spectrum.onRegionClicked = [this] (int slot) { showRegionPanel (slot); };

    spectrum.onDragStart = [this] (int slot, Handle handle) { beginGesture (slot, handle, true); };
    spectrum.onDragEnd   = [this] (int slot, Handle handle) { beginGesture (slot, handle, false); };
}

void DedeAudioProcessorEditor::showRegionPanel (int slot)
{
    if (! juce::isPositiveAndBelow (slot, maxRegions))
        return;

    // Anchor the callout on the region's own rectangle, so it points at the
    // band it belongs to rather than at the pointer.
    const auto area = getLocalArea (&spectrum, spectrum.getRegionArea (slot).toFloat())
                          .toNearestInt();

    juce::CallOutBox::launchAsynchronously (std::make_unique<RegionPanel> (processor, slot, lastRegionTab),
                                            area, this);
}

void DedeAudioProcessorEditor::showPresetBrowser()
{
    auto browser = std::make_unique<fxme::PresetComponent> (processor.getPresetManager());
    browser->setAccentColour (theme::accent);
    browser->setSize (320, 380);

    juce::CallOutBox::launchAsynchronously (std::move (browser),
                                            getLocalArea (&topBar,
                                                          presetBrowserButton.getBounds().toFloat())
                                                .toNearestInt(),
                                            this);
}

//==============================================================================
void DedeAudioProcessorEditor::timerCallback()
{
    refreshRegionsFromParameters (false);
}

void DedeAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Loading a preset can move every region at once, so the view is rebuilt
    // from the parameters rather than merely repainted.
    refreshRegionsFromParameters (true);
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

    // Four level knobs on the left, the three reverb knobs on the right, the
    // gate knee just left of the reverb, and whatever is left over as a gap.
    fxme::FxmeSlider* const levels[] { &dryKnob, &wetKnob, &revLevelKnob, &outKnob };
    fxme::FxmeSlider* const reverb[] { &roomKnob, &dampKnob, &widthKnob };

    const int knobW = juce::jmin (86, controls.getWidth() / 8);

    auto leftArea = controls.removeFromLeft (knobW * (int) std::size (levels));
    for (auto* k : levels)
        k->setBounds (leftArea.removeFromLeft (knobW).reduced (4));

    auto rightArea = controls.removeFromRight (knobW * (int) std::size (reverb));
    for (auto* k : reverb)
        k->setBounds (rightArea.removeFromLeft (knobW).reduced (4));

    // The gate section sits against the reverb group, leaving the gap on the
    // levels' side.
    kneeKnob.setBounds (controls.removeFromRight (knobW).reduced (4));
}
