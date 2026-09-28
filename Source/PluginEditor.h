/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    Dede's window: a top bar carrying the preset strip and the help button, the
    spectrum view where the regions are drawn (the whole point of the plugin),
    and one row of global controls along the bottom.

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

    DedeAudioProcessor& processor;
    fxme::FxmeLookAndFeel laf;

    fxme::TopBar topBar { "Dede", "spectral multi-delay and reverb",
                          JucePlugin_VersionString, juce::Image() };

    fxme::SpectrumRegionEditor spectrum;
    fxme::SpectrumRegionEditor::Region shownRegions[dede::maxRegions];

    /** One row per region slot, left of the spectrum: free, on, solo,
        keep in band, and the gate and level meters. */
    RegionColumn column { processor };

    /** The region popup's tab last shown, so the next one opens on it. Per
        editor session, not saved. The popup is a child of this editor, so the
        reference it keeps never outlives this. */
    int lastRegionTab = 0;

    // Global controls: levels on the left, the gate knee, then the shared
    // reverb on the right.
    fxme::FxmeSlider dryKnob      { processor.getAPVTS(), dede::ids::dryLevel, "Dry",     dede::theme::accent };
    fxme::FxmeSlider wetKnob      { processor.getAPVTS(), dede::ids::wetLevel, "Delays",  dede::theme::accent };
    fxme::FxmeSlider revLevelKnob { processor.getAPVTS(), dede::ids::revLevel, "Reverb",  dede::theme::specViolet };
    fxme::FxmeSlider outKnob      { processor.getAPVTS(), dede::ids::outLevel, "Output",  dede::theme::accent };
    fxme::FxmeSlider kneeKnob     { processor.getAPVTS(), dede::ids::gateKnee, "Knee",    dede::theme::accent };
    fxme::FxmeSlider roomKnob     { processor.getAPVTS(), dede::ids::revRoom,  "Room",    dede::theme::specViolet };
    fxme::FxmeSlider dampKnob     { processor.getAPVTS(), dede::ids::revDamp,  "Damping", dede::theme::specViolet };
    fxme::FxmeSlider widthKnob    { processor.getAPVTS(), dede::ids::revWidth, "Width",   dede::theme::specViolet };

    fxme::PresetBarComponent presetBar { processor.getPresetManager() };
    juce::TextButton presetBrowserButton { "..." };
    fxme::InfoButton infoButton;

    // The column's one-letter buttons rely on their tooltips. Declared after
    // the other children; it keeps itself on top anyway.
    juce::TooltipWindow tooltipWindow { this, 700 };

    // Exactly one per editor, declared after the child components.
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedeAudioProcessorEditor)
};
