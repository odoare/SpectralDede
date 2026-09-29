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
#include "GlobalPanel.h"

using namespace dede;

namespace
{
    const char* const helpBody =
        "Dede splits the incoming signal into frequency regions and gives each "
        "one its own stereo delay, plus a send to a shared reverb.\n\n"
        "Drawing regions:\n"
        "  drag on empty space to draw a new one\n"
        "  drag a border to move it, drag inside to move the whole band; a border\n"
        "    sticks to another region's for a few pixels, so the two meet exactly\n"
        "  drag the dashed line to set the gate: what rises above it passes\n"
        "  drag the dotted line to set the ceiling: what rises above it is cut\n"
        "    (at the top it is off; the two lines push each other apart)\n"
        "  drag the round handle for gain (up/down) and pan (left/right)\n"
        "  click a region to open its settings: delay; degrade (saturation,\n"
        "    bit-crush, downsample and shift, a pitch shift in semitones, each\n"
        "    placed before the delay, in its loop, where every repeat goes\n"
        "    through it again (shift there climbs by its interval at each\n"
        "    repeat), or after it); spectral (pitch in semitones, freeze tonal\n"
        "    or wash, blur)\n"
        "  a region's Dry adds its own band, panned and through the stages placed\n"
        "    before the delay, but not delayed,\n"
        "    on top of the global dry; its reverb send takes Dry and Delay after\n"
        "    their levels\n"
        "  Delete or Backspace removes the selected region\n\n"
        "Region column (one row per slot; click a row to select its region):\n"
        "  x frees the slot: the region is deleted, the slot can be drawn again\n"
        "  On off stops the band feeding the region: its tails ring out\n"
        "  S solos: every region not soloed is silenced (the dry signal is kept)\n"
        "  the bars: how much of the region's energy its gate lets through, then\n"
        "    its output level (left, right), shown even when silenced by a solo\n\n"
        "Delay times read three ways, chosen per region: seconds, a proportion "
        "of a whole note at the host tempo (0.25 is a quarter note), or a "
        "multiple of the period of the last MIDI note received. The panel shows "
        "what each side currently comes to in milliseconds.\n\n"
        "Spectrum view:\n"
        "  wheel zooms the level axis, ctrl+wheel the frequency axis\n"
        "  alt+drag or middle-drag pans, double-click resets\n"
        "  the view measures exactly as the gates do (same window size, peak\n"
        "    detector, no averaging), so a trace over a gate line passes it\n"
        "  in the Live engine the gates compare each band's whole level instead:\n"
        "    it shows as a shaded column in each region, topped by a solid line\n"
        "  the hold badge keeps the loudest levels for a while (off, 1, 3, 10 s,\n"
        "    or for good): a thin line over the spectrum, and in Live a dashed\n"
        "    line in each region, to place a gate against the loudest the sound\n"
        "    has been rather than against a moving trace. Changing it starts over\n\n"
        "Under the region rows: the levels of the dry signal, the summed "
        "delays, the reverb return and the output trim, then the output meter "
        "(RMS bars, peak markers held for a second, red over 0 dBFS).\n\n"
        "The gear button opens the global controls: the shared reverb (room, "
        "damping, width), the engine (Spectral, or Live for zero latency) and "
        "its FFT size, and the gate's attack, release and knee, shared by every "
        "region. Its own help button explains each.";
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

    globalButton.setTooltip ("Global controls: reverb, FFT size, gate timing and knee");
    globalButton.onClick = [this] { showGlobalPanel(); };

    infoButton.setColours ({ theme::accent, theme::text, theme::panel, theme::panelLine });
    infoButton.setInfo ("Dede", helpBody);
    topBar.setRightControls ({ { &presetBar, 210 }, { &presetBrowserButton, 26 },
                               { &globalButton, 24 }, { &infoButton, 22 } });

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

    // The trace has to show exactly what the gates and ceilings compare
    // against, so the view measures the way the splitter does:
    //  - the same window size (followed in timerCallback when it changes);
    //  - peak detector: where many bins share one point (the highs), the
    //    point shows the loudest, so the trace is over a gate line exactly
    //    when some bin there passes it (an average would draw a passing tone
    //    under the line);
    //  - no temporal averaging: the gate reacts to each frame as it is.
    // All three are locked, so they cannot drift from the DSP from the GUI.
    // The window is the same too (periodic Hann) and the dB scale is the
    // splitter's (FxmeTools' CoreSplitterTests pins that the analyser reads a
    // tone at exactly the gate's level).
    spectrum.setFftOrder (processor.getFftOrder());
    spectrum.setFftSizeLocked (true);
    {
        auto view = spectrum.getViewState();
        view.mode = fxme::SpectrumDisplay::Mode::peak;
        view.averaging = false;
        // Held maxima on by default: they are what a gate is best placed
        // against. The "hold" badge changes it (off, 1, 3, 10 s, for good).
        view.holdSeconds = 3.0f;
        spectrum.setViewState (view);
        spectrum.setMeasurementLocked (true);
    }

    spectrum.setNumRegions (maxRegions);
    spectrum.setGainRange (minGainDb, maxGainDb);
    spectrum.setGateRange (gateFloorDb, gateCeilDb);
    spectrum.setCeilingRange (gateFloorDb, gateCeilDb);
    spectrum.setCeilingEnabled (true);

    // A dragged border sticks to a neighbour's for a few pixels, so regions
    // can be made to meet exactly.
    spectrum.setEdgeSnapPixels (8.0f);
    wireRegionEditor();
    refreshRegionsFromParameters (true);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (column);
    addAndMakeVisible (outputMeter);

    setUpKnob (dryKnob,      theme::accent);
    setUpKnob (wetKnob,      theme::accent);
    setUpKnob (revLevelKnob, theme::specViolet);
    setUpKnob (outKnob,      theme::accent);

    processor.getPresetManager().addChangeListener (this);
    startTimerHz (20);

    setResizable (true, true);
    setResizeLimits (theme::minEditorWidth, 520, 2400, 1600);
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
    // A region that is off, or silenced by another's solo, is drawn
    // dimmed: desaturated and darker (the view sets its own alphas, so the
    // colour itself has to change).
    const bool heard = ! anySoloed() || value (ids::rgSolo) > 0.5f;
    const bool fed   = value (ids::rgActive) > 0.5f;
    r.colour = heard && fed ? theme::regionColour (slot)
                            : theme::regionColour (slot).withMultipliedSaturation (0.35f)
                                                         .withMultipliedBrightness (0.55f);
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
            || ! same (r.pan,    shown.pan)
            || r.colour != shown.colour;

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

    spectrum.onRegionDelete = [this] (int slot) { freeSlot (slot); };

    // The column and the view share the selection both ways.
    spectrum.onSelectionChanged = [this] (int slot) { column.setSelectedRow (slot); };
    column.onRowClicked = [this] (int slot)
    {
        spectrum.setSelectedRegion (slot);
        spectrum.grabKeyboardFocus();          // so Delete acts on it at once
    };
    column.onFreeClicked = [this] (int slot) { freeSlot (slot); };

    spectrum.onRegionClicked = [this] (int slot) { showRegionPanel (slot); };

    spectrum.onDragStart = [this] (int slot, Handle handle) { beginGesture (slot, handle, true); };
    spectrum.onDragEnd   = [this] (int slot, Handle handle) { beginGesture (slot, handle, false); };
}

void DedeAudioProcessorEditor::freeSlot (int slot)
{
    if (spectrum.getSelectedRegion() == slot)
        spectrum.setSelectedRegion (-1);

    setParameter (ids::region (slot, ids::rgOn), 0.0f);
    refreshRegionsFromParameters (true);
}

bool DedeAudioProcessorEditor::anySoloed() const
{
    auto& apvts = processor.getAPVTS();
    for (int i = 0; i < maxRegions; ++i)
    {
        auto* on   = apvts.getRawParameterValue (ids::region (i, ids::rgOn));
        auto* solo = apvts.getRawParameterValue (ids::region (i, ids::rgSolo));
        if (on != nullptr && solo != nullptr && on->load() > 0.5f && solo->load() > 0.5f)
            return true;
    }
    return false;
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

void DedeAudioProcessorEditor::showGlobalPanel()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<GlobalPanel> (processor),
                                            getLocalArea (&topBar,
                                                          globalButton.getBounds().toFloat())
                                                .toNearestInt(),
                                            this);
}

//==============================================================================
void DedeAudioProcessorEditor::timerCallback()
{
    refreshRegionsFromParameters (false);

    // Follow the splitter's window once a new FFT size has been applied (it
    // is applied asynchronously, so poll what the processor actually runs).
    const int order = processor.getFftOrder();
    if (order != spectrum.getFftOrder())
        spectrum.setFftOrder (order);

    // The live engine's gates compare each band's level, which sums every
    // bin of the band and so sits above the trace for anything noisy: show it
    // in each region, so the lines are set against what they are compared to.
    const bool live = processor.isLiveEngine();
    spectrum.setLevelMarkersEnabled (live);
    if (live)
        for (int i = 0; i < maxRegions; ++i)
            spectrum.setRegionLevel (i, processor.getBandLevelDb (i),
                                     processor.takeBandPeakLevelDb (i));
}

void DedeAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Loading a preset can move every region at once, so the view is rebuilt
    // from the parameters rather than merely repainted.
    refreshRegionsFromParameters (true);
    repaint();
}

juce::Image DedeAudioProcessorEditor::logoImage()
{
   #if DEDE_HAS_BINARY_DATA
    return juce::ImageCache::getFromMemory (BinaryData::logo686_png, BinaryData::logo686_pngSize);
   #else
    return {};
   #endif
}

void DedeAudioProcessorEditor::paint (juce::Graphics& g)
{
    theme::paintBackground (g, getLocalBounds().toFloat());
}

void DedeAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // A soft glow around the top bar's accent hairline: over the children, so
    // it spills a little into the bar above the line and into the margin
    // below it, and a brighter line on top. The margin below is empty, so
    // nothing is covered but background.
    const float lineY = (float) topBar.getBottom() - 1.5f;
    const float width = (float) getWidth();
    constexpr float above = 5.0f, below = 9.0f;

    g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.0f), 0.0f, lineY - above,
                                             theme::accent.withAlpha (0.22f), 0.0f, lineY, false));
    g.fillRect (juce::Rectangle<float> (0.0f, lineY - above, width, above));

    g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.30f), 0.0f, lineY + 1.5f,
                                             theme::accent.withAlpha (0.0f), 0.0f, lineY + 1.5f + below, false));
    g.fillRect (juce::Rectangle<float> (0.0f, lineY + 1.5f, width, below));

    g.setColour (theme::accent.withAlpha (0.9f));
    g.fillRect (juce::Rectangle<float> (0.0f, lineY, width, 1.5f));
}

void DedeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (theme::topBarHeight));

    area.reduce (theme::margin, theme::margin);

    // The left column, top to bottom: the region rows, which take whatever
    // height the knobs and the meter leave, the four level knobs in a 2 x 2
    // grid, and the output meter at the very bottom.
    auto left = area.removeFromLeft (theme::regionColumnWidth);
    area.removeFromLeft (theme::margin);
    spectrum.setBounds (area);

    outputMeter.setBounds (left.removeFromBottom (OutputMeter::preferredHeight));
    left.removeFromBottom (theme::margin);

    auto knobs = left.removeFromBottom (2 * theme::levelKnobHeight);
    left.removeFromBottom (theme::margin);
    column.setBounds (left);

    const int knobW = knobs.getWidth() / 2;
    fxme::FxmeSlider* const grid[] { &dryKnob, &wetKnob, &revLevelKnob, &outKnob };
    for (int i = 0; i < 4; ++i)
        grid[i]->setBounds (juce::Rectangle<int> (knobs.getX() + (i % 2) * knobW,
                                                  knobs.getY() + (i / 2) * theme::levelKnobHeight,
                                                  knobW, theme::levelKnobHeight).reduced (4));
}
