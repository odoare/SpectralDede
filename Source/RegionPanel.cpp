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
    constexpr int tabBarDepth  = 24;
    constexpr int pagePadding  = 6;
    constexpr int knobHeight   = 82;
    constexpr int footerHeight = 24;
    constexpr int panelMargin  = 8;
    constexpr int stripGap     = 12;    // between the tabs and the strip, a separator in it

    /** Knob widths across the panel: the dynamics page's row (the widest
        tab) and the strip. Every tab's knobs and the strip's share one width. */
    constexpr int tabUnits   = 9;
    constexpr int stripUnits = 5;
    constexpr int minKnobWidth = 58;

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
/** The region's delay: its two times and how they are read, and its three
    feedback gains. Its output levels are in the panel's strip. */
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
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("Time mode", modeBox.getBounds().translated (0, -16).withHeight (14),
                    juce::Justification::centred);
        g.drawText (timeText, readoutArea, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (pagePadding);

        // Five knobs, then the mode and what the times come to, two knobs
        // wide, on the right.
        auto knobs = area.removeFromTop (knobHeight);
        fxme::FxmeSlider* const row[] { timeL.get(), timeR.get(), fbL.get(), fbR.get(), fbX.get() };

        const int w = knobs.getWidth() / ((int) std::size (row) + 2);
        for (auto* k : row)
            k->setBounds (knobs.removeFromLeft (w).reduced (3));

        auto side = knobs.reduced (8, 0);
        modeBox.setBounds (side.withSizeKeepingCentre (juce::jmin (130, side.getWidth()), 20)
                               .translated (0, -10));
        readoutArea = side.withTop (modeBox.getBottom() + 6).withHeight (18);
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

        // The note mode has nothing to lock to until something has been played,
        // and silently reading the value as seconds would look like a bug.
        if (mode == fxme::DelayTimeMode::notePeriod && resolver.noteHz <= 0.0f)
            return "no MIDI note yet";

        return "L " + ms (parameterValue (processor, slot, ids::rgTimeL))
             + "   R " + ms (parameterValue (processor, slot, ids::rgTimeR));
    }

    DedeAudioProcessor& processor;
    const int slot;

    std::unique_ptr<fxme::FxmeSlider> timeL, timeR, fbL, fbR, fbX;
    juce::ComboBox modeBox;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    juce::String timeText;
    juce::Rectangle<int> readoutArea;
};

//==============================================================================
/** The region's dynamics around its two lines on the spectrum view: under
    the gate (the lower, dashed line) a downward expander, over the ceiling
    (the upper, dotted line) a compressor or a cut. The lines' levels are
    dragged on the view; this page sets what happens around them. A group is
    dimmed while its line is parked at the end of its range (off). */
class RegionPanel::DynamicsPage : public juce::Component,
                                  private juce::Timer
{
public:
    DynamicsPage (DedeAudioProcessor& p, int regionSlot, juce::Colour accentColour)
        : processor (p), slot (regionSlot), accent (accentColour)
    {
        const auto knob = [this] (const char* suffix, const char* label)
        {
            return makeKnob (*this, processor, slot, suffix, label, accent);
        };

        gate.push_back (knob (ids::rgGateRatio,   "Ratio"));
        gate.push_back (knob (ids::rgGateRange,   "Range"));
        gate.push_back (knob (ids::rgGateKnee,    "Knee"));
        gate.push_back (knob (ids::rgGateAttack,  "Attack"));
        gate.push_back (knob (ids::rgGateRelease, "Release"));

        ceiling.push_back (knob (ids::rgCeilRatio,   "Ratio"));
        ceiling.push_back (knob (ids::rgCeilKnee,    "Knee"));
        ceiling.push_back (knob (ids::rgCeilAttack,  "Attack"));
        ceiling.push_back (knob (ids::rgCeilRelease, "Release"));

        updateIdle();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        const auto heading = [&g, this] (juce::Rectangle<int> area, const juce::String& text, bool on)
        {
            g.setColour (on ? accent : theme::dimText);
            g.drawText (text, area.reduced (6, 0), juce::Justification::centredLeft);
        };
        heading (gateHeading, gateOn ? "Gate (lower line): expander"
                                     : "Gate: off (its line is at the bottom)", gateOn);
        heading (ceilingHeading, ceilingOn ? "Ceiling (upper line): " + ceilingMode()
                                           : "Ceiling: off (its line is at the top)", ceilingOn);

        g.setColour (theme::panelLine);
        g.fillRect (juce::Rectangle<float> ((float) separatorX, (float) pagePadding + 4.0f,
                                            1.0f, (float) getHeight() - pagePadding - 8.0f));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (pagePadding);

        const int unit = area.getWidth() / (int) (gate.size() + ceiling.size());
        auto gateArea = area.removeFromLeft (unit * (int) gate.size());
        separatorX = area.getX();

        constexpr int headingHeight = 22;
        gateHeading    = gateArea.removeFromTop (headingHeight);
        ceilingHeading = area.removeFromTop (headingHeight);

        const auto row = [unit] (juce::Rectangle<int> r, std::vector<std::unique_ptr<fxme::FxmeSlider>>& knobs)
        {
            r = r.withHeight (knobHeight);
            for (auto& k : knobs)
                k->setBounds (r.removeFromLeft (unit).reduced (3));
        };
        row (gateArea, gate);
        row (area, ceiling);
    }

private:
    void timerCallback() override { updateIdle(); }

    /** The ceiling's mode in words, from its ratio. */
    juce::String ceilingMode() const
    {
        const float v = parameterValue (processor, slot, ids::rgCeilRatio);
        return isCeilingCut (v) ? "cut" : v >= infiniteRatio - 0.5f ? "limiter" : "compressor";
    }

    /** A line parked at the end of its range is off: its group is dimmed. */
    void updateIdle()
    {
        const bool gateNow    = parameterValue (processor, slot, ids::rgGate) > gateFloorDb;
        const bool ceilingNow = parameterValue (processor, slot, ids::rgCeil) < gateCeilDb;
        const juce::String mode = ceilingMode();

        if (gateNow == gateOn && ceilingNow == ceilingOn && mode == shownMode)
            return;

        gateOn = gateNow;
        ceilingOn = ceilingNow;
        shownMode = mode;
        for (auto& k : gate)
            k->setAlpha (gateOn ? 1.0f : idleAlpha);
        for (auto& k : ceiling)
            k->setAlpha (ceilingOn ? 1.0f : idleAlpha);
        repaint();
    }

    DedeAudioProcessor& processor;
    const int slot;
    const juce::Colour accent;

    std::vector<std::unique_ptr<fxme::FxmeSlider>> gate, ceiling;
    juce::Rectangle<int> gateHeading, ceilingHeading;
    int separatorX = 0;
    bool gateOn = true, ceilingOn = true;
    juce::String shownMode;
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
    /** One control of a stage: a knob, or a choice box when `choices` is set
        (captioned with `label` above it). */
    struct Control
    {
        const char* suffix;
        juce::String label;
        const juce::StringArray* choices = nullptr;
        const char* tooltip = nullptr;
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
                    if (c.tooltip != nullptr)
                        box->setTooltip (c.tooltip);
                    addAndMakeVisible (*box);
                    stage->attachments.push_back (std::make_unique<ComboBoxAttachment> (
                        apvts, ids::region (slot, c.suffix), *box));
                    stage->controls.push_back (std::move (box));
                }
                else
                {
                    auto knob = makeKnob (*this, processor, slot, c.suffix, c.label, accentColour);
                    if (c.tooltip != nullptr)
                        knob->setTooltip (c.tooltip);
                    stage->controls.push_back (std::move (knob));
                }
                stage->labels.push_back (c.label);
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

        // A choice box's caption, above it (a knob draws its own label).
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        for (const auto& stage : stages)
            for (size_t i = 0; i < stage->controls.size(); ++i)
            {
                const auto& c = *stage->controls[i];
                if (dynamic_cast<const juce::ComboBox*> (&c) == nullptr || stage->labels[i].isEmpty())
                    continue;

                g.setColour (theme::dimText.withMultipliedAlpha (c.getAlpha()));
                g.drawText (stage->labels[i], c.getBounds().translated (0, -16).withHeight (14),
                            juce::Justification::centred);
            }
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
        std::vector<juce::String> labels;   // one per control
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

        bool captionsChanged = false;
        for (auto& stage : stages)
        {
            const float alpha = parameterValue (processor, slot, stage->onSuffix) > 0.5f ? 1.0f
                                                                                          : idleAlpha;
            for (auto& c : stage->controls)
                if (c->getAlpha() != alpha)
                {
                    c->setAlpha (alpha);
                    captionsChanged = true;
                }
            if (stage->positionBox != nullptr && stage->positionBox->getAlpha() != alpha)
                stage->positionBox->setAlpha (alpha);
        }

        if (captionsChanged)
            repaint();
    }

    DedeAudioProcessor& processor;
    const int slot;

    // Attachments are declared inside each StageUi after its controls, so they
    // are destroyed first, while the controls they attach to still exist.
    std::vector<std::unique_ptr<StageUi>> stages;
    std::vector<int> separators;
};

//==============================================================================
/** The region's output, beside the tabs and always in view: its level and
    pan (the round handle on the spectrum), its own band undelayed (Dry), its
    repeats (Delay), and the send to the shared reverb taken after those two. */
class RegionPanel::Strip : public juce::Component
{
public:
    Strip (DedeAudioProcessor& processor, int slot, juce::Colour accent)
    {
        const auto knob = [&] (const char* suffix, const char* label, const char* tooltip)
        {
            auto k = makeKnob (*this, processor, slot, suffix, label, accent);
            k->setTooltip (tooltip);
            return k;
        };

        gain   = knob (ids::rgGain, "Gain",   "The region's level: also the round handle's height");
        pan    = knob (ids::rgPan,  "Pan",    "The region's pan: also the round handle's place across it");
        dry    = knob (ids::rgDry,  "Dry",    "The region's own band, not delayed (panned, through the "
                                              "stages placed before the delay), on top of the global dry");
        delay  = knob (ids::rgDly,  "Delay",  "The region's repeats");
        reverb = knob (ids::rgSend, "Reverb", "Send to the shared reverb, taken after Dry and Delay");

        gain->setCentralValue (0.0);
        pan->setCentralValue (0.0);
    }

    void resized() override
    {
        auto row = getLocalBounds().removeFromTop (knobHeight);
        const int w = row.getWidth() / stripUnits;
        for (auto* k : { gain.get(), pan.get(), dry.get(), delay.get(), reverb.get() })
            k->setBounds (row.removeFromLeft (w).reduced (3));
    }

private:
    std::unique_ptr<fxme::FxmeSlider> gain, pan, dry, delay, reverb;
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
      tabs (std::make_unique<Tabs> (lastTab)),
      strip (std::make_unique<Strip> (processor, regionSlot, accent))
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

    using Stage = StagesPage::Stage;

    // In signal order within a position: saturation, bit-crush, downsample,
    // transpose, freq shift. Each chooses its own position: before, in or after the delay.
    const std::vector<Stage> degrade {
        { ids::rgSatOn,   "Saturation", { { ids::rgSatModel,  "Model", &saturationModelNames },
                                          { ids::rgSatDrive,  "Drive" } }, ids::rgSatPos },
        { ids::rgCrushOn, "Bit-crush",  { { ids::rgCrushBits, "Bits" } },  ids::rgCrushPos },
        { ids::rgDsOn,    "Downsample", { { ids::rgDsFactor,  "Factor" } }, ids::rgDsPos },
        { ids::rgTransposeOn, "Transpose",  { { ids::rgTransposeSt,   "Semitones" } }, ids::rgTransposePos },
        { ids::rgFreqShiftOn, "Freq shift", { { ids::rgFreqShiftHz, "Hz" } }, ids::rgFreqShiftPos },
    };

    // Inside the splitter's frame: freeze or blur, then pitch.
    const std::vector<Stage> spectralStages {
        { ids::rgPitchOn,  "Pitch",  { { ids::rgPitchSt,    "Semitones" } } },
        { ids::rgFreezeOn, "Freeze", {
            { ids::rgFreezeMode,  "Mode", &freezeModeNames,
              "Tonal: held notes stay notes. Wash: a smooth, static texture" },
            { ids::rgFreezeTrig,  "Trigger", &freezeTriggerNames,
              "Manual: frozen while Freeze is on. Retrigger: captures again at every step "
              "of the rate. On/off: frozen for one step, live for the next" },
            { ids::rgFreezeRate,  "Rate", &freezeRateNames,
              "Step of the Retrigger and On/off triggers, synced to the host tempo "
              "(T: triplet, D: dotted)" },
            { ids::rgFreezeWidth, "Width", nullptr,
              "How far the frozen left and right channels drift apart in phase: "
              "0 keeps the image, 100 % makes them independent. Stereo bands only" } } },
        { ids::rgBlurOn,   "Blur",   { { ids::rgBlurMs,     "Time" } } },
    };

    // The tabs in signal order: the dynamics act on the band as it is cut
    // out, the spectral effects inside the same frame, then the delay and
    // the degrade stages placed around it.
    tabs->addTab ("Dynamics", theme::panel, new DynamicsPage (processor, slot, accent), true);

    // Pitch, freeze and blur need the FFT: in the Live engine the tab is
    // dimmed and says so, its settings kept for when the engine comes back.
    auto* spectralPage = new StagesPage (processor, slot, accent, spectralStages);
    spectralPage->isUnavailable = [&processor] { return processor.isLiveEngine(); };
    spectralPage->setTooltip ("Pitch, freeze and blur work on the FFT: they have no "
                              "effect in the Live engine");
    tabs->addTab (processor.isLiveEngine() ? "Spectral (off in Live)" : "Spectral",
                  theme::panel, spectralPage, true);

    tabs->addTab ("Delay",   theme::panel, new DelayPage  (processor, slot, accent), true);
    tabs->addTab ("Degrade", theme::panel, new StagesPage (processor, slot, accent, degrade), true);

    tabs->setCurrentTabIndex (juce::jlimit (0, tabs->getNumTabs() - 1, wanted));
    tabs->tracking = true;
    addAndMakeVisible (*tabs);
    addAndMakeVisible (*strip);
}

int RegionPanel::preferredHeight()
{
    return tabBarDepth + pageHeight + 2 * panelMargin;
}

int RegionPanel::minimumWidth()
{
    return (tabUnits + stripUnits) * minKnobWidth + stripGap + 2 * panelMargin;
}

juce::Rectangle<int> RegionPanel::stripArea() const
{
    auto area = getLocalBounds().reduced (panelMargin);
    const int unit = (area.getWidth() - stripGap) / (tabUnits + stripUnits);
    return area.removeFromRight (stripUnits * unit);
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

    // The strip's heading, level with the tab bar: which region this is.
    auto heading = stripArea().removeFromTop (tabBarDepth).reduced (3, 2);
    auto chip = heading.removeFromLeft (heading.getHeight() + 4).toFloat();
    g.setColour (accent);
    g.fillRoundedRectangle (chip, 3.0f);
    g.setColour (juce::Colours::black);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    g.drawText (juce::String (slot + 1), chip, juce::Justification::centred);

    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    g.drawText ("Region " + juce::String (slot + 1) + " output", heading.withTrimmedLeft (8),
                juce::Justification::centredLeft);

    // Between the tabs and the strip.
    const auto strip = stripArea();
    g.setColour (theme::panelLine);
    g.fillRect (juce::Rectangle<float> ((float) strip.getX() - (float) stripGap * 0.5f,
                                        (float) panelMargin + 4.0f, 1.0f,
                                        (float) getHeight() - 2.0f * (float) panelMargin - 8.0f));
}

void RegionPanel::resized()
{
    auto area = getLocalBounds().reduced (panelMargin);
    const auto stripBounds = stripArea();
    tabs->setBounds (area.withRight (stripBounds.getX() - stripGap));
    strip->setBounds (stripBounds.withTrimmedTop (tabBarDepth + pagePadding));
}
