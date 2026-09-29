/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    Dede's window: a top bar carrying the preset strip, the global controls
    (gear) and help buttons; the spectrum view where the regions are drawn
    (the whole point of the plugin); and on its left a column with one row per
    region, the four levels (dry, delays, reverb return, output) and the
    output meter.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "RegionColumn.h"
#include "GlobalPanel.h"
#include "OutputMeter.h"

//==============================================================================
class DedeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::ChangeListener,
                                 private juce::Timer
{
public:
    explicit DedeAudioProcessorEditor (DedeAudioProcessor&);
    ~DedeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    /** Rotary style, accent, label and visibility in one call — every global
        knob is set up identically. */
    void setUpKnob (fxme::FxmeSlider& knob, juce::Colour accent);

    //==========================================================================
    // Region editing. The parameters stay the single source of truth: the view
    // is refreshed from them on a timer (so automation and preset loads show
    // up with no extra wiring), and every gesture in the view writes back
    // through the host.

    void wireRegionEditor();
    void refreshRegionsFromParameters (bool force);
    fxme::SpectrumRegionEditor::Region regionFromParameters (int slot) const;

    void setParameter (const juce::String& id, float value);

    /** Deletes the region in `slot` and releases the slot (from the view's
        Delete key or the column's Free button). */
    void freeSlot (int slot);

    /** True when any region in use is soloed. */
    bool anySoloed() const;
    void beginGesture (int slot, fxme::SpectrumRegionEditor::Handle handle, bool begin);

    /** First slot whose "on" parameter is false, or -1 when all are in use. */
    int findFreeSlot() const;

    /** Opens one region's delay settings in a callout anchored on it. */
    void showRegionPanel (int slot);

    /** Opens the full preset browser in a callout under the top bar. The
        compact strip there only walks the list; saving, renaming and deleting
        live here. */
    void showPresetBrowser();

    /** Opens the global controls (reverb, engine, gate) in a callout under
        the gear button. */
    void showGlobalPanel();

    DedeAudioProcessor& processor;
    fxme::FxmeLookAndFeel laf;

    fxme::TopBar topBar { "Dede", "spectral multi-delay and reverb",
                          JucePlugin_VersionString, logoImage() };

    /** A soft glow around the top bar's accent hairline, spilling a little
        into the bar and into the margin below it. A child of its own, added
        right after the top bar, so it sits above the bar but below everything
        added later: the region and preset callouts cover it. It ignores the
        mouse. */
    class HairlineGlow : public juce::Component
    {
    public:
        HairlineGlow() { setInterceptsMouseClicks (false, false); }

        /** How far the glow reaches above and below the line, in pixels. */
        static constexpr int above = 5, below = 9;

        void paint (juce::Graphics&) override;
    };
    HairlineGlow hairlineGlow;

    /** The FX-Mechanics logo, from the binary data (empty without it). */
    static juce::Image logoImage();

    fxme::SpectrumRegionEditor spectrum;
    fxme::SpectrumRegionEditor::Region shownRegions[dede::maxRegions];

    /** One row per region slot, left of the spectrum: free, on, solo,
        and the gate and level meters. */
    RegionColumn column { processor };
    OutputMeter outputMeter { processor };

    /** The region popup's tab last shown, so the next one opens on it. Per
        editor session, not saved. The popup is a child of this editor, so the
        reference it keeps never outlives this. */
    int lastRegionTab = 0;

    // The four levels, under the region rows. The reverb's other controls,
    // the engine and the gate are in the global controls callout.
    fxme::FxmeSlider dryKnob      { processor.getAPVTS(), dede::ids::dryLevel, "Dry",     dede::theme::accent };
    fxme::FxmeSlider wetKnob      { processor.getAPVTS(), dede::ids::wetLevel, "Delays",  dede::theme::accent };
    fxme::FxmeSlider revLevelKnob { processor.getAPVTS(), dede::ids::revLevel, "Reverb",  dede::theme::specViolet };
    fxme::FxmeSlider outKnob      { processor.getAPVTS(), dede::ids::outLevel, "Output",  dede::theme::accent };

    fxme::PresetBarComponent presetBar { processor.getPresetManager() };
    juce::TextButton presetBrowserButton { "..." };
    GearButton globalButton;
    fxme::InfoButton infoButton;

    // The column's one-letter buttons rely on their tooltips. Declared after
    // the other children; it keeps itself on top anyway.
    juce::TooltipWindow tooltipWindow { this, 700 };

    // Exactly one per editor, declared after the child components.
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedeAudioProcessorEditor)
};
