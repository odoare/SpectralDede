/*
  ------------------------------------------------------------------------------
    RegionColumn.cpp

    See RegionColumn.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "RegionColumn.h"

using namespace dede;

namespace
{
    constexpr int   rowGap       = 3;
    constexpr int   chipWidth    = 20;
    constexpr int   buttonHeight = 20;
    constexpr int   meterStrip   = 26;       // gate bar, gap, L and R bars
    constexpr float meterFloorDb = -60.0f;

    // Solo keeps its usual mixer colour, whatever the region's.
    const juce::Colour soloColour { 0xffd8cf45 };

    float parameterValue (DedeAudioProcessor& processor, int slot, const char* suffix)
    {
        auto* p = processor.getAPVTS().getRawParameterValue (ids::region (slot, suffix));
        return p != nullptr ? p->load() : 0.0f;
    }
}

//==============================================================================
class RegionColumn::Row : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    Row (RegionColumn& ownerColumn, DedeAudioProcessor& p, int regionSlot)
        : owner (ownerColumn), processor (p), slot (regionSlot),
          colour (theme::regionColour (regionSlot))
    {
        freeButton.setButtonText (juce::String::charToString (0x00d7));   // multiplication sign
        freeButton.setTooltip ("Free: delete this region; its slot can be drawn again");
        freeButton.setColour (juce::TextButton::buttonColourId, theme::panel.brighter (0.15f));
        freeButton.setColour (juce::TextButton::textColourOffId, theme::dimText);
        freeButton.setMouseClickGrabsKeyboardFocus (false);
        freeButton.onClick = [this]
        {
            if (owner.onFreeClicked != nullptr)
                owner.onFreeClicked (slot);
        };
        addChildComponent (freeButton);

        setUpToggle (active, "On", ids::rgActive, colour,
                     "On: the region is fed by its band. Off: the band stops "
                     "feeding it and its tails ring out");
        setUpToggle (solo, "S", ids::rgSolo, soloColour,
                     "Solo: silence every region not soloed (the dry signal is kept)");

        setTooltip ("Region " + juce::String (slot + 1) + ": click to select it");
    }

    /** Pulls parameter state and meter levels; repaints when anything shown
        has changed. Called from the column's timer. */
    void update (bool soloing)
    {
        const bool used = parameterValue (processor, slot, ids::rgOn) > 0.5f;
        if (used != inUse)
        {
            inUse = used;
            for (auto* c : std::initializer_list<juce::Component*> { &freeButton, &active, &solo })
                c->setVisible (inUse);
            if (! inUse)
                gateShown = levelShown[0] = levelShown[1] = 0.0f;
            repaint();
        }

        if (! inUse)
            return;

        const bool heard = ! soloing || parameterValue (processor, slot, ids::rgSolo) > 0.5f;
        const bool fed = parameterValue (processor, slot, ids::rgActive) > 0.5f;
        const bool dim = ! heard || ! fed;
        if (dim != dimmed)
        {
            dimmed = dim;
            repaint();
        }

        // Meters rise at once and fall at a steady rate, as meters do.
        const auto toFraction = [] (float db)
        {
            return juce::jlimit (0.0f, 1.0f, (db - meterFloorDb) / -meterFloorDb);
        };
        const auto fall = [] (float shown, float now) { return juce::jmax (now, shown - 0.04f); };

        const float gate = processor.getGateOpenness (slot);
        const float l = toFraction (processor.getRegionLevelDb (slot, 0));
        const float r = toFraction (processor.getRegionLevelDb (slot, 1));

        const float newGate = fall (gateShown, gate);
        const float newL = fall (levelShown[0], l);
        const float newR = fall (levelShown[1], r);

        if (std::abs (newGate - gateShown) > 0.002f
            || std::abs (newL - levelShown[0]) > 0.002f
            || std::abs (newR - levelShown[1]) > 0.002f)
        {
            gateShown = newGate;
            levelShown[0] = newL;
            levelShown[1] = newR;
            repaint (meterArea());
        }
    }

    void setSelected (bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;
        selected = shouldBeSelected;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (selected && inUse ? colour.withAlpha (0.18f) : theme::panel);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (selected && inUse ? colour.withAlpha (0.8f) : theme::panelLine);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        // The slot's number on a chip of its colour: all a free slot shows.
        const auto chip = chipArea().toFloat();
        const float chipAlpha = ! inUse ? 0.25f : dimmed ? 0.45f : 1.0f;
        g.setColour (colour.withAlpha (chipAlpha));
        g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (inUse ? juce::Colours::black : theme::dimText);
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        g.drawText (juce::String (slot + 1), chip, juce::Justification::centred);

        if (! inUse)
            return;

        // Gate meter, then the stereo level meter, as vertical bars.
        auto meters = meterArea().toFloat();
        const auto bar = [&g, &meters] (float width, float fraction, juce::Colour c)
        {
            auto b = meters.removeFromLeft (width);
            g.setColour (theme::plotBack);
            g.fillRect (b);
            g.setColour (c);
            g.fillRect (b.removeFromBottom (b.getHeight() * fraction));
        };

        const auto meterColour = colour.withAlpha (dimmed ? 0.45f : 0.9f);
        bar (8.0f, gateShown, meterColour.withMultipliedBrightness (0.8f));
        meters.removeFromLeft (4.0f);
        bar (5.0f, levelShown[0], meterColour);
        meters.removeFromLeft (2.0f);
        bar (5.0f, levelShown[1], meterColour);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 0);
        area.removeFromLeft (chipWidth + 4);
        area.removeFromRight (meterStrip + 4);

        auto line = area.withSizeKeepingCentre (area.getWidth(), buttonHeight);

        freeButton.setBounds (line.removeFromLeft (18));
        line.removeFromLeft (4);

        // On is two letters, S one.
        const int unit = juce::jmax (16, (line.getWidth() - 3) / 3);
        active.setBounds (line.removeFromLeft (2 * unit));
        line.removeFromLeft (3);
        solo.setBounds (line.removeFromLeft (unit));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (inUse && owner.onRowClicked != nullptr)
            owner.onRowClicked (slot);
    }

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setUpToggle (fxme::AccentToggle& t, const char* text, const char* suffix,
                      juce::Colour accent, const char* tooltip)
    {
        t.setButtonText (text);
        t.setAccent (accent, theme::dimText);
        t.setTooltip (tooltip);
        addChildComponent (t);
        attachments.push_back (std::make_unique<ButtonAttachment> (
            processor.getAPVTS(), ids::region (slot, suffix), t));
    }

    juce::Rectangle<int> chipArea() const
    {
        return getLocalBounds().reduced (4).removeFromLeft (chipWidth)
                   .withSizeKeepingCentre (chipWidth, juce::jmin (chipWidth, getHeight() - 8));
    }

    juce::Rectangle<int> meterArea() const
    {
        return getLocalBounds().reduced (4).removeFromRight (meterStrip);
    }

    RegionColumn& owner;
    DedeAudioProcessor& processor;
    const int slot;
    const juce::Colour colour;

    juce::TextButton freeButton;
    fxme::AccentToggle active, solo;

    // After the buttons, so the attachments go first when the row is destroyed.
    std::vector<std::unique_ptr<ButtonAttachment>> attachments;

    bool inUse = false, dimmed = false, selected = false;
    float gateShown = 0.0f;
    float levelShown[2] { 0.0f, 0.0f };
};

//==============================================================================
RegionColumn::RegionColumn (DedeAudioProcessor& p)
    : processor (p)
{
    for (int i = 0; i < maxRegions; ++i)
    {
        rows[(size_t) i] = std::make_unique<Row> (*this, processor, i);
        addAndMakeVisible (*rows[(size_t) i]);
    }

    timerCallback();
    startTimerHz (30);
}

RegionColumn::~RegionColumn() = default;

void RegionColumn::setSelectedRow (int slot)
{
    for (int i = 0; i < maxRegions; ++i)
        rows[(size_t) i]->setSelected (i == slot);
}

void RegionColumn::resized()
{
    auto area = getLocalBounds();
    const int rowHeight = (area.getHeight() - (maxRegions - 1) * rowGap) / maxRegions;

    for (int i = 0; i < maxRegions; ++i)
    {
        rows[(size_t) i]->setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (rowGap);
    }
}

void RegionColumn::timerCallback()
{
    bool soloing = false;
    for (int i = 0; i < maxRegions; ++i)
        soloing = soloing || (parameterValue (processor, i, ids::rgOn) > 0.5f
                              && parameterValue (processor, i, ids::rgSolo) > 0.5f);

    for (auto& row : rows)
        row->update (soloing);
}
