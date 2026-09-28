/*
  ------------------------------------------------------------------------------
    RegionColumn.h

    The column on the left of the spectrum view: one row per region slot, in
    the slot's colour, with what a mixer strip would show at a glance.

      - Free (x): deletes the region and releases its slot, which can then be
        drawn again on the spectrum. A momentary action.
      - On: the region is fed by its band. Off, the band stops feeding it and
        its tails ring out; the region stays in place and editable.
      - S (solo): every region not soloed is silenced. Several can be on. The
        global dry is not affected.
      - K (keep in band): band-passes the region back to its range after its
        degrade chain (the same switch as on the Degrade tab).
      - A gate meter: how much of the region's energy its gate and ceiling let
        through.
      - A stereo meter: the region's output level, shown even when another
        region's solo silences it.

    A free slot shows only its number. Clicking a row selects its region on
    the spectrum view, and the selected region's row is highlighted.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

#include <array>
#include <functional>
#include <memory>

class RegionColumn : public juce::Component,
                     private juce::Timer
{
public:
    explicit RegionColumn (DedeAudioProcessor& processor);
    ~RegionColumn() override;

    /** A row was clicked outside its buttons: select that region. */
    std::function<void (int slot)> onRowClicked;

    /** A row's Free button was clicked: delete that region. */
    std::function<void (int slot)> onFreeClicked;

    /** Highlights the row of the region selected on the spectrum view, or
        none with -1. */
    void setSelectedRow (int slot);

    void resized() override;

private:
    class Row;

    void timerCallback() override;

    DedeAudioProcessor& processor;
    std::array<std::unique_ptr<Row>, dede::maxRegions> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionColumn)
};
