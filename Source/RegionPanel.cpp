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
#include "Tooltips.h"

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
    constexpr int stripUnits = 3;
    constexpr int minKnobWidth = 58;

    constexpr int selectorWidth = 56;       // a degrade stage's position selector

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

    /** Sets region `slot`'s `suffix` parameter as one host gesture. */
    void setParameterValue (DedeAudioProcessor& processor, int slot, const char* suffix, float value)
    {
        if (auto* p = processor.getAPVTS().getParameter (ids::region (slot, suffix)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }

    constexpr int switchHeight = 22;   // a stage's switch, over its controls

    /** A stage switch, drawn like the parameter-bound ones, whose state is
        not a parameter of its own: it follows one (a line parked at the end
        of its range, a level at its bottom). Clicking it calls switchOff or
        switchOn, which move that parameter; refresh() reads it back. */
    class SlavedSwitch : public juce::ToggleButton
    {
    public:
        SlavedSwitch (const juce::String& text, juce::Colour accent)
        {
            setButtonText (text);
            setColour (juce::ToggleButton::tickColourId, accent);
            setClickingTogglesState (false);
            onClick = [this]
            {
                const auto action = getToggleState() ? switchOff : switchOn;
                if (action != nullptr)
                    action();
                refresh();
            };
        }

        std::function<bool()> isOn;
        std::function<void()> switchOn, switchOff;

        /** Follows the parameter; returns true when the state changed. */
        bool refresh()
        {
            const bool on = isOn != nullptr && isOn();
            if (on == getToggleState())
                return false;
            setToggleState (on, juce::dontSendNotification);
            return true;
        }
    };
}

//==============================================================================
/** The region's delay: its two times and how they are read, and its three
    feedback gains. Its output levels are in the panel's strip. Its switch
    follows the strip's Delay level: off at the bottom (-inf), where it
    parks it, remembering where it was. */
class RegionPanel::DelayPage : public juce::Component,
                               private juce::Timer
{
public:
    DelayPage (DedeAudioProcessor& p, int regionSlot, juce::Colour accentColour, Session& sessionToUse)
        : processor (p), slot (regionSlot), session (sessionToUse), delaySwitch ("Delay", accentColour)
    {
        delaySwitch.setTooltip (tips::delay::delaySwitch);
        delaySwitch.isOn = [this] { return parameterValue (processor, slot, ids::rgDly) > minFeedbackDb; };
        delaySwitch.switchOff = [this]
        {
            session.delayDb[slot] = parameterValue (processor, slot, ids::rgDly);
            setParameterValue (processor, slot, ids::rgDly, minFeedbackDb);
        };
        delaySwitch.switchOn = [this]
        {
            const float db = session.delayDb[slot];
            setParameterValue (processor, slot, ids::rgDly, db > minFeedbackDb ? db : defaultDelayLevelDb);
        };
        addAndMakeVisible (delaySwitch);

        const auto knob = [this, accentColour] (const char* suffix, const char* label)
        {
            return makeKnob (*this, processor, slot, suffix, label, accentColour);
        };

        timeL      = knob (ids::rgTimeL, "Time L");
        timeR      = knob (ids::rgTimeR, "Time R");
        fbL        = knob (ids::rgFbL,   "Fb L");
        fbR        = knob (ids::rgFbR,   "Fb R");
        fbX        = knob (ids::rgFbX,   "Cross");
        timeL->setTooltip (tips::delay::timeL);
        timeR->setTooltip (tips::delay::timeR);
        fbL->setTooltip (tips::delay::fbL);
        fbR->setTooltip (tips::delay::fbR);
        fbX->setTooltip (tips::delay::cross);
        modeBox.setTooltip (tips::delay::mode);

        modeBox.addItem ("Seconds",   1);
        modeBox.addItem ("DAW sync",  2);
        modeBox.addItem ("MIDI note", 3);
        theme::styleCombo (modeBox, accentColour);
        addAndMakeVisible (modeBox);
        modeAttachment = std::make_unique<ComboBoxAttachment> (
            processor.getAPVTS(), ids::region (slot, ids::rgMode), modeBox);

        timeText = resolvedTimeText();
        updateIdle();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::dimText.withMultipliedAlpha (shownAlpha));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("Time mode", modeBox.getBounds().translated (0, -16).withHeight (14),
                    juce::Justification::centred);
        g.drawText (timeText, readoutArea, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (pagePadding);
        delaySwitch.setBounds (area.removeFromTop (switchHeight).reduced (6, 0));

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

    /** Off, the delay's controls are dimmed, still editable. */
    void updateIdle()
    {
        delaySwitch.refresh();
        const float alpha = delaySwitch.getToggleState() ? 1.0f : idleAlpha;
        for (auto* c : std::initializer_list<juce::Component*> { timeL.get(), timeR.get(), fbL.get(),
                                                                  fbR.get(), fbX.get(), &modeBox })
            c->setAlpha (alpha);
        if (alpha != shownAlpha)
        {
            shownAlpha = alpha;
            repaint();   // the caption and the read-out
        }
    }

    void timerCallback() override
    {
        updateIdle();

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
    Session& session;

    SlavedSwitch delaySwitch;
    float shownAlpha = 1.0f;
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
    dragged on the view; this page sets what happens around them. Each group
    has a switch that follows its line: off while it is parked at the end of
    its range, where switching off parks it (remembering where it was). */
class RegionPanel::DynamicsPage : public juce::Component,
                                  private juce::Timer
{
public:
    DynamicsPage (DedeAudioProcessor& p, int regionSlot, juce::Colour accentColour, Session& sessionToUse)
        : processor (p), slot (regionSlot), accent (accentColour), session (sessionToUse),
          gateSwitch ("Gate", accentColour), ceilingSwitch ("Ceiling", accentColour)
    {
        // Brought back, a line keeps clear of the other one (when that one
        // is on), as dragging them on the view would.
        static constexpr float clearance = 6.0f;
        const auto gateDb = [this] { return parameterValue (processor, slot, ids::rgGate); };
        const auto ceilDb = [this] { return parameterValue (processor, slot, ids::rgCeil); };

        gateSwitch.setTooltip (tips::dynamics::gateSwitch);
        gateSwitch.isOn = [gateDb] { return gateDb() > gateFloorDb; };
        gateSwitch.switchOff = [this, gateDb]
        {
            session.gateDb[slot] = gateDb();
            setParameterValue (processor, slot, ids::rgGate, gateFloorDb);
        };
        gateSwitch.switchOn = [this, ceilDb]
        {
            float db = session.gateDb[slot];
            if (ceilDb() < gateCeilDb)
                db = juce::jmin (db, ceilDb() - clearance);
            setParameterValue (processor, slot, ids::rgGate, juce::jlimit (gateFloorDb + 1.0f, gateCeilDb - 1.0f, db));
        };

        ceilingSwitch.setTooltip (tips::dynamics::ceilingSwitch);
        ceilingSwitch.isOn = [ceilDb] { return ceilDb() < gateCeilDb; };
        ceilingSwitch.switchOff = [this, ceilDb]
        {
            session.ceilingDb[slot] = ceilDb();
            setParameterValue (processor, slot, ids::rgCeil, gateCeilDb);
        };
        ceilingSwitch.switchOn = [this, gateDb]
        {
            float db = session.ceilingDb[slot];
            if (gateDb() > gateFloorDb)
                db = juce::jmax (db, gateDb() + clearance);
            setParameterValue (processor, slot, ids::rgCeil, juce::jlimit (gateFloorDb + 1.0f, gateCeilDb - 1.0f, db));
        };

        addAndMakeVisible (gateSwitch);
        addAndMakeVisible (ceilingSwitch);

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

        for (auto [k, tip] : { std::pair { gate[0].get(), tips::dynamics::gateRatio },
                               std::pair { gate[1].get(), tips::dynamics::gateRange },
                               std::pair { gate[2].get(), tips::dynamics::gateKnee },
                               std::pair { gate[3].get(), tips::dynamics::gateAttack },
                               std::pair { gate[4].get(), tips::dynamics::gateRelease },
                               std::pair { ceiling[0].get(), tips::dynamics::ceilRatio },
                               std::pair { ceiling[1].get(), tips::dynamics::ceilKnee },
                               std::pair { ceiling[2].get(), tips::dynamics::ceilAttack },
                               std::pair { ceiling[3].get(), tips::dynamics::ceilRelease } })
            k->setTooltip (tip);

        updateIdle();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
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

        gateSwitch.setBounds (gateArea.removeFromTop (switchHeight).reduced (6, 0));
        ceilingSwitch.setBounds (area.removeFromTop (switchHeight).reduced (6, 0));

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

    /** A line parked at the end of its range is off: its switch says so
        and its group is dimmed. The switch also names what the group is. */
    void updateIdle()
    {
        gateSwitch.refresh();
        ceilingSwitch.refresh();
        const bool gateOn = gateSwitch.getToggleState(), ceilingOn = ceilingSwitch.getToggleState();

        const juce::String gateText    = gateOn ? "Gate: expander" : "Gate";
        const juce::String ceilingText = ceilingOn ? "Ceiling: " + ceilingMode() : "Ceiling";
        if (gateSwitch.getButtonText() != gateText)
            gateSwitch.setButtonText (gateText);
        if (ceilingSwitch.getButtonText() != ceilingText)
            ceilingSwitch.setButtonText (ceilingText);

        for (auto& k : gate)
            k->setAlpha (gateOn ? 1.0f : idleAlpha);
        for (auto& k : ceiling)
            k->setAlpha (ceilingOn ? 1.0f : idleAlpha);
    }

    DedeAudioProcessor& processor;
    const int slot;
    const juce::Colour accent;
    Session& session;

    SlavedSwitch gateSwitch, ceilingSwitch;
    std::vector<std::unique_ptr<fxme::FxmeSlider>> gate, ceiling;
    int separatorX = 0;
};

//==============================================================================
/** Where a degrade stage runs: three small radio buttons stacked vertically
    (Before, In loop, After), left of the stage's knobs, bound to its position
    choice parameter. */
class PositionSelector : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    PositionSelector (juce::RangedAudioParameter& parameter, juce::Colour accentColour)
        : accent (accentColour),
          attachment (parameter, [this] (float v) { setSelected (juce::roundToInt (v)); })
    {
        setTooltip (tips::degrade::position);
        attachment.sendInitialUpdate();
    }

    /** The rows' height together, for centring. */
    static constexpr int rowHeight = 15;
    static int preferredHeight() { return rowHeight * degradePositionNames.size(); }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        for (int i = 0; i < degradePositionNames.size(); ++i)
        {
            auto row = rowArea (i);
            const bool on = i == selected, hot = i == hovered;

            const auto dot = row.removeFromLeft (rowHeight).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (on ? accent : (hot ? theme::dimText : theme::panelLine.brighter (0.3f)));
            g.drawEllipse (dot, 1.2f);
            if (on)
                g.fillEllipse (dot.reduced (2.2f));

            g.setColour (on ? theme::text : (hot ? theme::text.withAlpha (0.8f) : theme::dimText));
            g.drawText (degradePositionNames[i], row.withTrimmedLeft (2),
                        juce::Justification::centredLeft, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (rowAt (e.y)); }
    void mouseExit (const juce::MouseEvent&) override   { setHovered (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int row = rowAt (e.y);
        if (row >= 0 && row != selected)
            attachment.setValueAsCompleteGesture ((float) row);
    }

private:
    juce::Rectangle<int> rowArea (int i) const
    {
        const int top = (getHeight() - preferredHeight()) / 2;
        return { 0, top + i * rowHeight, getWidth(), rowHeight };
    }

    int rowAt (int y) const
    {
        for (int i = 0; i < degradePositionNames.size(); ++i)
            if (y >= rowArea (i).getY() && y < rowArea (i).getBottom())
                return i;
        return -1;
    }

    void setSelected (int index)
    {
        if (index != selected)
        {
            selected = index;
            repaint();
        }
    }

    void setHovered (int index)
    {
        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }

    const juce::Colour accent;
    int selected = -1, hovered = -1;
    juce::ParameterAttachment attachment;   // last: it calls setSelected while constructed
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
        choice of where it runs, left of its controls. */
    struct Stage
    {
        const char* onSuffix;
        juce::String title;
        std::vector<Control> controls;
        const char* positionSuffix = nullptr;
        const char* tooltip = nullptr;   // the stage's switch
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
            if (spec.tooltip != nullptr)
                stage->onSwitch->button.setTooltip (spec.tooltip);
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
                if (auto* position = apvts.getParameter (ids::region (slot, spec.positionSuffix)))
                {
                    stage->positionSelector = std::make_unique<PositionSelector> (*position, accentColour);
                    addAndMakeVisible (*stage->positionSelector);
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
        // The position selectors have a fixed width; the rest is shared out
        // in units, a knob one, a choice box a little more (its text needs
        // it more than a knob's disc does).
        static constexpr float boxUnits = 1.4f;
        const auto unitsOf = [] (const juce::Component& c)
        {
            return dynamic_cast<const juce::ComboBox*> (&c) != nullptr ? boxUnits : 1.0f;
        };
        const auto stageUnits = [&unitsOf] (const StageUi& stage)
        {
            float units = 0.0f;
            for (const auto& c : stage.controls)
                units += unitsOf (*c);
            return juce::jmax (1.0f, units);
        };

        float totalUnits = 0.0f;
        int selectors = 0;
        for (const auto& stage : stages)
        {
            totalUnits += stageUnits (*stage);
            selectors += stage->positionSelector != nullptr ? 1 : 0;
        }

        const float unit = (float) (area.getWidth() - selectors * selectorWidth) / totalUnits;

        separators.clear();
        for (size_t i = 0; i < stages.size(); ++i)
        {
            auto& stage = *stages[i];
            const int width = juce::roundToInt (stageUnits (stage) * unit)
                            + (stage.positionSelector != nullptr ? selectorWidth : 0);
            auto column = i + 1 == stages.size() ? area : area.removeFromLeft (width);

            if (i > 0)
                separators.push_back (column.getX());

            stage.onSwitch->setBounds (column.removeFromTop (switchHeight).reduced (6, 0));

            if (stage.positionSelector != nullptr)
                stage.positionSelector->setBounds (column.removeFromLeft (selectorWidth).withTrimmedLeft (6));

            for (auto& c : stage.controls)
            {
                auto cell = column.removeFromLeft (juce::roundToInt (unitsOf (*c) * unit));
                if (dynamic_cast<juce::ComboBox*> (c.get()) != nullptr)
                    c->setBounds (cell.withSizeKeepingCentre (juce::jmin (110, cell.getWidth() - 6), 20));
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
        std::unique_ptr<PositionSelector> positionSelector;
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
            if (stage->positionSelector != nullptr && stage->positionSelector->getAlpha() != alpha)
                stage->positionSelector->setAlpha (alpha);
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
/** The region's output, beside the tabs and always in view: its own band
    undelayed (Dry), its repeats (Delay), and the send to the shared reverb
    taken after those two. Its gain and pan are the line and the round handle
    on the spectrum. */
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

        dry    = knob (ids::rgDry,  "Dry",    tips::strip::dry);
        delay  = knob (ids::rgDly,  "Delay",  tips::strip::delay);
        reverb = knob (ids::rgSend, "Reverb", tips::strip::reverb);
    }

    void resized() override
    {
        auto row = getLocalBounds().removeFromTop (knobHeight);
        const int w = row.getWidth() / stripUnits;
        for (auto* k : { dry.get(), delay.get(), reverb.get() })
            k->setBounds (row.removeFromLeft (w).reduced (3));
    }

private:
    std::unique_ptr<fxme::FxmeSlider> dry, delay, reverb;
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
RegionPanel::Session::Session()
{
    std::fill (std::begin (gateDb),    std::end (gateDb),    -60.0f);
    std::fill (std::begin (ceilingDb), std::end (ceilingDb), -12.0f);
    std::fill (std::begin (delayDb),   std::end (delayDb),   defaultDelayLevelDb);
}

RegionPanel::RegionPanel (DedeAudioProcessor& processor, int regionSlot, Session& session)
    : slot (regionSlot), accent (theme::regionColour (regionSlot)),
      tabs (std::make_unique<Tabs> (session.lastTab)),
      strip (std::make_unique<Strip> (processor, regionSlot, accent))
{
    const int wanted = session.lastTab;

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
        { ids::rgSatOn,   "Saturation", { { ids::rgSatModel,  "Model", &saturationModelNames, tips::degrade::model },
                                          { ids::rgSatDrive,  "Drive", nullptr, tips::degrade::drive } },
          ids::rgSatPos, tips::degrade::saturation },
        { ids::rgCrushOn, "Bit-crush",  { { ids::rgCrushBits, "Bits", nullptr, tips::degrade::bits } },
          ids::rgCrushPos, tips::degrade::crush },
        { ids::rgDsOn,    "Downsample", { { ids::rgDsFactor,  "Factor", nullptr, tips::degrade::factor } },
          ids::rgDsPos, tips::degrade::downsample },
        { ids::rgTransposeOn, "Transpose",  { { ids::rgTransposeSt, "Semitones", nullptr, tips::degrade::semitones } },
          ids::rgTransposePos, tips::degrade::transpose },
        { ids::rgFreqShiftOn, "Freq shift", { { ids::rgFreqShiftHz, "Hz", nullptr, tips::degrade::hz } },
          ids::rgFreqShiftPos, tips::degrade::freqShift },
    };

    // Inside the splitter's frame: freeze or blur, then pitch.
    const std::vector<Stage> spectralStages {
        { ids::rgPitchOn,  "Pitch",  { { ids::rgPitchSt, "Semitones", nullptr, tips::spectral::semitones } },
          nullptr, tips::spectral::pitch },
        { ids::rgFreezeOn, "Freeze", {
            { ids::rgFreezeMode,  "Mode",    &freezeModeNames,    tips::spectral::freezeMode },
            { ids::rgFreezeTrig,  "Trigger", &freezeTriggerNames, tips::spectral::trigger },
            { ids::rgFreezeRate,  "Rate",    &freezeRateNames,    tips::spectral::rate },
            { ids::rgFreezeWidth, "Width",   nullptr,             tips::spectral::width } },
          nullptr, tips::spectral::freeze },
        { ids::rgBlurOn,   "Blur",   { { ids::rgBlurMs, "Time", nullptr, tips::spectral::blurTime } },
          nullptr, tips::spectral::blur },
    };

    // The tabs in signal order: the dynamics act on the band as it is cut
    // out, the spectral effects inside the same frame, then the delay and
    // the degrade stages placed around it.
    tabs->addTab ("Dynamics", theme::panel, new DynamicsPage (processor, slot, accent, session), true);

    // Pitch, freeze and blur need the FFT: in the Live engine the tab is
    // dimmed and says so, its settings kept for when the engine comes back.
    auto* spectralPage = new StagesPage (processor, slot, accent, spectralStages);
    spectralPage->isUnavailable = [&processor] { return processor.isLiveEngine(); };
    spectralPage->setTooltip (tips::spectral::page);
    tabs->addTab (processor.isLiveEngine() ? "Spectral (off in Live)" : "Spectral",
                  theme::panel, spectralPage, true);

    tabs->addTab ("Delay",   theme::panel, new DelayPage  (processor, slot, accent, session), true);
    tabs->addTab ("Degrade", theme::panel, new StagesPage (processor, slot, accent, degrade), true);

    tabs->setCurrentTabIndex (juce::jlimit (0, tabs->getNumTabs() - 1, wanted));
    tabs->tracking = true;
    addAndMakeVisible (*tabs);
    addAndMakeVisible (*strip);

    presetBar = std::make_unique<fxme::PresetBarComponent> (processor.getRegionPresets (slot));
    presetBar->setAccentColour (accent);
    presetBar->setBrowserButtonVisible (true);
    presetBar->setBrowserSize (300, 360);
    addAndMakeVisible (*presetBar);
}

juce::Rectangle<int> RegionPanel::headingArea() const
{
    return stripArea().withTrimmedTop (tabBarDepth + pagePadding).removeFromTop (switchHeight).reduced (3, 1);
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

    // The strip's heading, level with the tabs' switches: which region this
    // is, then its preset bar (a child).
    auto heading = headingArea();
    auto chip = heading.removeFromLeft (heading.getHeight() + 4).toFloat();
    g.setColour (accent);
    g.fillRoundedRectangle (chip, 3.0f);
    g.setColour (juce::Colours::black);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    g.drawText (juce::String (slot + 1), chip, juce::Justification::centred);

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
    strip->setBounds (stripBounds.withTrimmedTop (tabBarDepth + pagePadding + switchHeight));

    auto heading = headingArea();
    heading.removeFromLeft (heading.getHeight() + 4 + 6);   // the chip and a gap
    presetBar->setBounds (heading);
}
