/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    Dede's window: a top bar carrying the preset strip, the global controls
    (gear) and help buttons; the spectrum view where the regions are drawn
    (the whole point of the plugin), with the selected region's settings
    docked under it; and on its left a column with one row per region, the
    four levels (dry, delays, reverb return, output) and the output meter.

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
#include "RegionPanel.h"
#include "AppSettings.h"
#include "Tooltips.h"

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

    /** Puts every parameter of region slot `slot` but its "on" back to its
        default, for a region drawn anew. */
    void resetSlotToDefaults (int slot);

    /** Copies every setting of region `source` (but its solo) into a free
        slot and puts it in use; returns that slot, or -1 when none is free.
        For the view's Ctrl-drag. */
    int duplicateRegion (int source);

    /** Deletes the region in `slot` and releases the slot (from the view's
        Delete key or the column's Free button). */
    void freeSlot (int slot);

    /** True when any region in use is soloed. */
    bool anySoloed() const;
    void beginGesture (int slot, fxme::SpectrumRegionEditor::Handle handle, bool begin);

    /** First slot whose "on" parameter is false, or -1 when all are in use. */
    int findFreeSlot() const;

    /** Docks the settings of region `slot` under the spectrum, or the hint
        to select one with -1. Does nothing when that one is already shown. */
    void showRegion (int slot);

    /** Opens the global controls (reverb, engine, gate) in a callout under
        the gear button. */
    void showGlobalPanel();

    DedeAudioProcessor& processor;
    fxme::FxmeLookAndFeel laf;

    fxme::TopBar topBar { dede::theme::displayName, "Spectral delay, degrade & more",
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

    /** An embedded image by its binary-data name ("splash_jpg"); invalid
        when the assets are not built in. */
    static juce::Image assetImage (const char* resourceName);

    /** The about / splash screen: Dede's artwork over the window, with the
        FX-Mechanics logo and a link to fx-mechanics.com under it. Shown for
        3 s the first time the editor opens in a plugin instance, and until
        clicked when the top bar's logo or Dede's icon is clicked. Over
        every other child. */
    fxme::SplashOverlay splash;

    fxme::SpectrumRegionEditor spectrum;
    fxme::SpectrumRegionEditor::Region shownRegions[dede::maxRegions];

    /** One row per region slot, left of the spectrum: free, on, solo,
        and the gate and level meters. */
    RegionColumn column { processor };
    OutputMeter outputMeter { processor };

    /** The selected region's settings, under the spectrum; null when none
        is selected (the dock then shows a hint, painted by the editor). A new
        panel is made for each selection. */
    std::unique_ptr<RegionPanel> regionPanel;
    juce::Rectangle<int> dockArea;

    /** What the dock remembers from one region to the next (its tab, where
        switched-off lines were). Per editor session, not saved. The panel is
        a child of this editor, so the reference it keeps never outlives this. */
    RegionPanel::Session regionSession;

    // The four levels, under the region rows. The reverb's other controls,
    // the engine and the gate are in the global controls callout.
    fxme::FxmeSlider dryKnob      { processor.getAPVTS(), dede::ids::dryLevel, "Dry",     dede::theme::accent };
    fxme::FxmeSlider wetKnob      { processor.getAPVTS(), dede::ids::wetLevel, "Delays",  dede::theme::accent };
    fxme::FxmeSlider revLevelKnob { processor.getAPVTS(), dede::ids::revLevel, "Reverb",  dede::theme::specViolet };
    fxme::FxmeSlider outKnob      { processor.getAPVTS(), dede::ids::outLevel, "Output",  dede::theme::accent };

    fxme::PresetBarComponent presetBar { processor.getPresetManager() };
    GearButton globalButton;
    fxme::InfoButton infoButton;

    // The column's one-letter buttons rely on their tooltips. Declared after
    // the other children; it keeps itself on top anyway.
    // The hover help, with an off switch (the "?" of the global controls,
    // saved machine-wide in AppSettings). Asking per tip rather than
    // destroying the window: one object, and the switch covers every
    // component under the editor, callouts included.
    struct ToggleableTooltipWindow : public juce::TooltipWindow
    {
        using juce::TooltipWindow::TooltipWindow;
        juce::String getTipFor (juce::Component& c) override
        {
            return dede::getUiTooltips() ? juce::TooltipWindow::getTipFor (c) : juce::String();
        }
    };
    ToggleableTooltipWindow tooltipWindow { this, 700 };

    // Exactly one per editor, declared after the child components.
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedeAudioProcessorEditor)
};
