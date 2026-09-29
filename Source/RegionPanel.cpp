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
    constexpr int numKnobs     = 8;     // the delay page's row sets the width
    constexpr int panelMargin  = 8;

    constexpr int positionRowHeight = 24;   // a degrade stage's position selector

    constexpr int pageHeight = pagePadding + knobHeight + footerHeight + positionRowHeight;

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
/** The region's delay (times, feedback) and its three output levels: its
    own band (Dry), its repeats (Delay), and the reverb send taken from both. */
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
        dryLevel   = knob (ids::rgDry,   "Dry");
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
                                        fbX.get(), dryLevel.get(), delayLevel.get(), send.get() };

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

    std::unique_ptr<fxme::FxmeSlider> timeL, timeR, fbL, fbR, fbX, dryLevel, delayLevel, send;
    juce::ComboBox modeBox;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    juce::String timeText;
};

//==============================================================================
/** A page of switchable stages side by side, each a column headed by its own
    on/off switch over its controls (knobs and choice boxes). The controls of a
    stage that is off are dimmed but stay editable, so a stage can be set up
    before it is heard. Both the Degrade and the Spectral tabs are one. */
class RegionPanel::StagesPage : public juce::Component,
                                public juce::SettableTooltipClient,
                                private juce::Timer
{
public:
    /** One control of a stage: a knob, or a choice box when `choices` is set. */
    struct Control
    {
        const char* suffix;
        juce::String label;
        const juce::StringArray* choices = nullptr;
    };

    /** One stage: its switch, its title, its controls, and optionally the
        choice of where it runs, shown under its controls. */
    struct Stage
    {
        const char* onSuffix;
        juce::String title;
        std::vector<Control> controls;
        const char* positionSuffix = nullptr;
    };

    StagesPage (DedeAudioProcessor& p, int regionSlot, juce::Colour accentColour,
                const std::vector<Stage>& stageSpecs)
        : processor (p), slot (regionSlot)
    {
        auto& apvts = processor.getAPVTS();

        for (const auto& spec : stageSpecs)
        {
            auto stage = std::make_unique<StageUi>();
            stage->onSuffix = spec.onSuffix;
            stage->onSwitch = std::make_unique<fxme::FxmeButton> (
                apvts, ids::region (slot, spec.onSuffix), spec.title, accentColour);
            addAndMakeVisible (*stage->onSwitch);

            for (const auto& c : spec.controls)
            {
                if (c.choices != nullptr)
                {
                    auto box = std::make_unique<juce::ComboBox>();
                    box->addItemList (*c.choices, 1);
                    theme::styleCombo (*box, accentColour);
                    addAndMakeVisible (*box);
                    stage->attachments.push_back (std::make_unique<ComboBoxAttachment> (
                        apvts, ids::region (slot, c.suffix), *box));
                    stage->controls.push_back (std::move (box));
                }
                else
                {
                    stage->controls.push_back (makeKnob (*this, processor, slot, c.suffix,
                                                         c.label, accentColour));
                }
            }

            if (spec.positionSuffix != nullptr)
            {
                stage->positionBox = std::make_unique<juce::ComboBox>();
                stage->positionBox->addItemList (degradePositionNames, 1);
                stage->positionBox->setTooltip ("Where this stage runs: before the delay (the region's "
                                                "Dry hears it too), in the delay loop (the first echo, "
                                                "then again on every repeat), or after the delay");
                theme::styleCombo (*stage->positionBox, accentColour);
                addAndMakeVisible (*stage->positionBox);
                stage->attachments.push_back (std::make_unique<ComboBoxAttachment> (
                    apvts, ids::region (slot, spec.positionSuffix), *stage->positionBox));
            }

            stages.push_back (std::move (stage));
        }

        updateIdleStages();
        startTimerHz (10);
    }

    /** When set and true, the whole page is dimmed: its stages have no
        effect in the current engine (the Spectral tab in the Live engine).
        The controls stay editable, for when the engine comes back. */
    std::function<bool()> isUnavailable;

    void paint (juce::Graphics& g) override
    {
        // Thin separators between the stages.
        g.setColour (theme::panelLine);
        for (auto x : separators)
            g.fillRect (juce::Rectangle<float> ((float) x, (float) pagePadding + 4.0f,
                                                1.0f, (float) getHeight() - pagePadding - 8.0f));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (pagePadding);

        // Every control gets the same width, so a stage with two controls is
        // twice as wide as one with one.
        int totalControls = 0;
        for (const auto& stage : stages)
            totalControls += juce::jmax (1, (int) stage->controls.size());

        const int unit = area.getWidth() / juce::jmax (1, totalControls);
        constexpr int switchHeight = 22;

        separators.clear();
        for (size_t i = 0; i < stages.size(); ++i)
        {
            auto& stage = *stages[i];
            const int n = juce::jmax (1, (int) stage.controls.size());
            auto column = i + 1 == stages.size() ? area : area.removeFromLeft (n * unit);

            if (i > 0)
                separators.push_back (column.getX());

            stage.onSwitch->setBounds (column.removeFromTop (switchHeight).reduced (6, 0));

            if (stage.positionBox != nullptr)
            {
                auto row = column.removeFromBottom (positionRowHeight);
                stage.positionBox->setBounds (row.withSizeKeepingCentre (juce::jmin (110, row.getWidth() - 8), 20));
            }

            for (auto& c : stage.controls)
            {
                auto cell = column.removeFromLeft (unit);
                if (dynamic_cast<juce::ComboBox*> (c.get()) != nullptr)
                    c->setBounds (cell.withSizeKeepingCentre (juce::jmin (110, cell.getWidth() - 8), 20));
                else
                    c->setBounds (cell.reduced (3));
            }
        }
    }

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct StageUi
    {
        const char* onSuffix = nullptr;
        std::unique_ptr<fxme::FxmeButton> onSwitch;
        std::vector<std::unique_ptr<juce::Component>> controls;
        std::unique_ptr<juce::ComboBox> positionBox;
        std::vector<std::unique_ptr<ComboBoxAttachment>> attachments;
    };

    void timerCallback() override { updateIdleStages(); }

    /** Dims the controls of every stage that is switched off, and the whole
        page when it is unavailable. */
    void updateIdleStages()
    {
        const float pageAlpha = isUnavailable != nullptr && isUnavailable() ? idleAlpha : 1.0f;
        if (getAlpha() != pageAlpha)
            setAlpha (pageAlpha);

        for (auto& stage : stages)
        {
            const float alpha = parameterValue (processor, slot, stage->onSuffix) > 0.5f ? 1.0f
                                                                                          : idleAlpha;
            for (auto& c : stage->controls)
                if (c->getAlpha() != alpha)
                    c->setAlpha (alpha);
            if (stage->positionBox != nullptr && stage->positionBox->getAlpha() != alpha)
                stage->positionBox->setAlpha (alpha);
        }
    }

    DedeAudioProcessor& processor;
    const int slot;

    // Attachments are declared inside each StageUi after its controls, so they
    // are destroyed first, while the controls they attach to still exist.
    std::vector<std::unique_ptr<StageUi>> stages;
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
    using Stage = StagesPage::Stage;

    // In signal order within a position: saturation, bit-crush, downsample,
    // shift. Each chooses its own position: before, in or after the delay.
    const std::vector<Stage> degrade {
        { ids::rgSatOn,   "Saturation", { { ids::rgSatModel,  "Model", &saturationModelNames },
                                          { ids::rgSatDrive,  "Drive" } }, ids::rgSatPos },
        { ids::rgCrushOn, "Bit-crush",  { { ids::rgCrushBits, "Bits" } },  ids::rgCrushPos },
        { ids::rgDsOn,    "Downsample", { { ids::rgDsFactor,  "Factor" } }, ids::rgDsPos },
        { ids::rgShiftOn, "Shift",      { { ids::rgShiftSt,   "Semitones" } }, ids::rgShiftPos },
    };

    // Inside the splitter's frame: freeze or blur, then pitch.
    const std::vector<Stage> spectralStages {
        { ids::rgPitchOn,  "Pitch",  { { ids::rgPitchSt,    "Semitones" } } },
        { ids::rgFreezeOn, "Freeze", { { ids::rgFreezeMode, "Mode", &freezeModeNames } } },
        { ids::rgBlurOn,   "Blur",   { { ids::rgBlurMs,     "Time" } } },
    };

    tabs->addTab ("Degrade",  theme::panel, new StagesPage (processor, slot, accent, degrade), true);
    // Pitch, freeze and blur need the FFT: in the Live engine the tab is
    // dimmed and says so, its settings kept for when the engine comes back.
    auto* spectralPage = new StagesPage (processor, slot, accent, spectralStages);
    spectralPage->isUnavailable = [&processor] { return processor.isLiveEngine(); };
    spectralPage->setTooltip ("Pitch, freeze and blur work on the FFT: they have no "
                              "effect in the Live engine");
    tabs->addTab (processor.isLiveEngine() ? "Spectral (off in Live)" : "Spectral",
                  theme::panel, spectralPage, true);

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
