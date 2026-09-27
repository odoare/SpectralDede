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
    constexpr int titleHeight  = 20;
    constexpr int tabBarDepth  = 24;
    constexpr int pagePadding  = 6;
    constexpr int knobHeight   = 82;
    constexpr int footerHeight = 24;
    constexpr int knobWidth    = 74;
    constexpr int numKnobs     = 7;     // the delay page's row sets the width
    constexpr int panelMargin  = 8;

    constexpr int pageHeight = pagePadding + knobHeight + footerHeight;

    /** Alpha of the controls of a degrade stage that is switched off: still
        editable (so a stage can be set up before it is heard), visibly idle. */
    constexpr float idleAlpha = 0.45f;

    /** One knob bound to region `slot`'s `suffix` parameter, already a child
        of `parent`. */
    std::unique_ptr<fxme::FxmeSlider> makeKnob (juce::Component& parent,
                                                DedeAudioProcessor& processor, int slot,
                                                const char* suffix, const juce::String& label,
                                                juce::Colour accent)
    {
        auto knob = std::make_unique<fxme::FxmeSlider> (processor.getAPVTS(),
                                                        ids::region (slot, suffix),
                                                        label, accent);
        knob->setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob->setShowLabel (true);
        theme::styleKnob (*knob, accent);
        parent.addAndMakeVisible (*knob);
        return knob;
    }

    float parameterValue (DedeAudioProcessor& processor, int slot, const char* suffix)
    {
        auto* p = processor.getAPVTS().getRawParameterValue (ids::region (slot, suffix));
        return p != nullptr ? p->load() : 0.0f;
    }
}

//==============================================================================
/** The region's delay: times, feedback, and the two output levels. */
class RegionPanel::DelayPage : public juce::Component,
                               private juce::Timer
{
public:
    DelayPage (DedeAudioProcessor& p, int regionSlot, juce::Colour accentColour)
        : processor (p), slot (regionSlot)
    {
        const auto knob = [this, accentColour] (const char* suffix, const char* label)
        {
            return makeKnob (*this, processor, slot, suffix, label, accentColour);
        };

        timeL      = knob (ids::rgTimeL, "Time L");
        timeR      = knob (ids::rgTimeR, "Time R");
        fbL        = knob (ids::rgFbL,   "Fb L");
        fbR        = knob (ids::rgFbR,   "Fb R");
        fbX        = knob (ids::rgFbX,   "Cross");
        delayLevel = knob (ids::rgDly,   "Delay");
        send       = knob (ids::rgSend,  "Reverb");

        modeBox.addItem ("Seconds",   1);
        modeBox.addItem ("DAW sync",  2);
        modeBox.addItem ("MIDI note", 3);
        theme::styleCombo (modeBox, accentColour);
        addAndMakeVisible (modeBox);
        modeAttachment = std::make_unique<ComboBoxAttachment> (
            processor.getAPVTS(), ids::region (slot, ids::rgMode), modeBox);

        timeText = resolvedTimeText();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::dimText);
        g.setFont (11.0f);
        g.drawText (timeText, getLocalBounds().removeFromBottom (footerHeight),
                    juce::Justification::centredRight);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (pagePadding);

        auto knobs = area.removeFromTop (knobHeight);
        fxme::FxmeSlider* const row[] { timeL.get(), timeR.get(), fbL.get(), fbR.get(),
                                        fbX.get(), delayLevel.get(), send.get() };

        const int w = knobs.getWidth() / numKnobs;
        for (auto* k : row)
            k->setBounds (knobs.removeFromLeft (w).reduced (3));

        modeBox.setBounds (area.removeFromLeft (110).withSizeKeepingCentre (110, 20));
    }

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override
    {
        const auto text = resolvedTimeText();
        if (text == timeText)
            return;

        timeText = text;
        repaint();
    }

    /** What the two delay values currently come to in milliseconds, which is
        the only way to read a sync or note-locked time at a glance. */
    juce::String resolvedTimeText() const
    {
        fxme::DelayTimeResolver resolver;
        resolver.bpm        = processor.getCurrentBpm();
        resolver.noteHz     = processor.getLastNoteHz();
        resolver.maxSeconds = maxDelaySeconds;

        const auto mode = (fxme::DelayTimeMode) juce::jlimit (
            0, numDelayModes - 1, (int) parameterValue (processor, slot, ids::rgMode));

        const auto ms = [&resolver, mode] (float v)
        {
            return juce::String (resolver.toSeconds (v, mode) * 1000.0f, 1) + " ms";
        };

        juce::String text = "L " + ms (parameterValue (processor, slot, ids::rgTimeL))
                          + "    R " + ms (parameterValue (processor, slot, ids::rgTimeR));

        // The note mode has nothing to lock to until something has been played,
        // and silently reading the value as seconds would look like a bug.
        if (mode == fxme::DelayTimeMode::notePeriod && resolver.noteHz <= 0.0f)
            text += "    (no MIDI note yet)";

        return text;
    }

    DedeAudioProcessor& processor;
    const int slot;

    std::unique_ptr<fxme::FxmeSlider> timeL, timeR, fbL, fbR, fbX, delayLevel, send;
    juce::ComboBox modeBox;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    juce::String timeText;
};

//==============================================================================
/** The region's degrade chain: three stages side by side, in signal order,
    each headed by its own switch. */
class RegionPanel::DegradePage : public juce::Component,
                                 private juce::Timer
{
public:
    DegradePage (DedeAudioProcessor& p, int regionSlot, juce::Colour accentColour)
        : processor (p), slot (regionSlot)
    {
        auto& apvts = processor.getAPVTS();

        satOn   = std::make_unique<fxme::FxmeButton> (apvts, ids::region (slot, ids::rgSatOn),
                                                      "Saturation", accentColour);
        crushOn = std::make_unique<fxme::FxmeButton> (apvts, ids::region (slot, ids::rgCrushOn),
                                                      "Bit-crush", accentColour);
        dsOn    = std::make_unique<fxme::FxmeButton> (apvts, ids::region (slot, ids::rgDsOn),
                                                      "Downsample", accentColour);
        for (auto* b : { satOn.get(), crushOn.get(), dsOn.get() })
            addAndMakeVisible (*b);

        satDrive = makeKnob (*this, processor, slot, ids::rgSatDrive, "Drive", accentColour);
        bits     = makeKnob (*this, processor, slot, ids::rgCrushBits, "Bits", accentColour);
        factor   = makeKnob (*this, processor, slot, ids::rgDsFactor, "Factor", accentColour);

        satModel.addItemList (saturationModelNames, 1);
        theme::styleCombo (satModel, accentColour);
        addAndMakeVisible (satModel);
        satModelAttachment = std::make_unique<ComboBoxAttachment> (
            apvts, ids::region (slot, ids::rgSatModel), satModel);

        updateIdleStages();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        // Thin separators between the three stages.
        g.setColour (theme::panelLine);
        for (auto x : separators)
            g.fillRect (juce::Rectangle<float> ((float) x, (float) pagePadding + 4.0f,
                                                1.0f, (float) getHeight() - pagePadding - 8.0f));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (pagePadding);

        // Saturation has two controls (model and drive), the others one: four
        // equal units, two of them for saturation.
        const int unit = area.getWidth() / 4;
        auto sat   = area.removeFromLeft (2 * unit);
        auto crush = area.removeFromLeft (unit);
        auto ds    = area;

        separators = { crush.getX(), ds.getX() };

        constexpr int switchHeight = 22;

        const auto layStage = [switchHeight] (juce::Rectangle<int> column, juce::Component& sw)
        {
            sw.setBounds (column.removeFromTop (switchHeight).reduced (6, 0));
            return column;
        };

        auto satBody = layStage (sat, *satOn);
        satModel.setBounds (satBody.removeFromLeft (satBody.getWidth() / 2)
                                   .withSizeKeepingCentre (110, 20));
        satDrive->setBounds (satBody.reduced (3));

        bits->setBounds (layStage (crush, *crushOn).reduced (3));
        factor->setBounds (layStage (ds, *dsOn).reduced (3));
    }

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override { updateIdleStages(); }

    /** Dims the controls of every stage that is switched off. */
    void updateIdleStages()
    {
        const auto dim = [this] (const char* onSuffix, std::initializer_list<juce::Component*> controls)
        {
            const float alpha = parameterValue (processor, slot, onSuffix) > 0.5f ? 1.0f : idleAlpha;
            for (auto* c : controls)
                if (c->getAlpha() != alpha)
                    c->setAlpha (alpha);
        };

        dim (ids::rgSatOn,   { &satModel, satDrive.get() });
        dim (ids::rgCrushOn, { bits.get() });
        dim (ids::rgDsOn,    { factor.get() });
    }

    DedeAudioProcessor& processor;
    const int slot;

    std::unique_ptr<fxme::FxmeButton> satOn, crushOn, dsOn;
    std::unique_ptr<fxme::FxmeSlider> satDrive, bits, factor;
    juce::ComboBox satModel;
    std::unique_ptr<ComboBoxAttachment> satModelAttachment;
    std::vector<int> separators;
};

//==============================================================================
/** Tabs that report the page shown, so the editor can reopen on it. */
class RegionPanel::Tabs : public juce::TabbedComponent
{
public:
    explicit Tabs (int& lastTabToTrack)
        : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop), lastTab (lastTabToTrack)
    {
    }

    void currentTabChanged (int index, const juce::String&) override
    {
        if (index >= 0 && tracking)
            lastTab = index;
    }

    /** Off while the tabs are being built: adding the first tab selects it,
        which would otherwise overwrite the remembered one. */
    bool tracking = false;

private:
    int& lastTab;
};

//==============================================================================
RegionPanel::RegionPanel (DedeAudioProcessor& processor, int regionSlot, int& lastTab)
    : slot (regionSlot), accent (theme::regionColour (regionSlot)),
      tabs (std::make_unique<Tabs> (lastTab))
{
    const int wanted = lastTab;

    tabs->setTabBarDepth (tabBarDepth);
    tabs->setOutline (0);
    tabs->setIndent (0);
    tabs->setColour (juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
    tabs->setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);

    auto& bar = tabs->getTabbedButtonBar();
    bar.setColour (juce::TabbedButtonBar::tabTextColourId,     theme::dimText);
    bar.setColour (juce::TabbedButtonBar::frontTextColourId,   accent);
    bar.setColour (juce::TabbedButtonBar::tabOutlineColourId,  juce::Colours::transparentBlack);
    bar.setColour (juce::TabbedButtonBar::frontOutlineColourId, accent.withAlpha (0.7f));

    tabs->addTab ("Delay",   theme::panel, new DelayPage   (processor, slot, accent), true);
    tabs->addTab ("Degrade", theme::panel, new DegradePage (processor, slot, accent), true);

    tabs->setCurrentTabIndex (juce::jlimit (0, tabs->getNumTabs() - 1, wanted));
    tabs->tracking = true;
    addAndMakeVisible (*tabs);

    setSize (numKnobs * knobWidth + 2 * panelMargin,
             titleHeight + tabBarDepth + pageHeight + 2 * panelMargin);
}

RegionPanel::~RegionPanel() = default;

//==============================================================================
void RegionPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (accent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    g.drawText ("Region " + juce::String (slot + 1),
                getLocalBounds().reduced (panelMargin).removeFromTop (titleHeight),
                juce::Justification::centredLeft);
}

void RegionPanel::resized()
{
    auto area = getLocalBounds().reduced (panelMargin);
    area.removeFromTop (titleHeight);
    tabs->setBounds (area);
}
