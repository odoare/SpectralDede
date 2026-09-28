/*
  ------------------------------------------------------------------------------
    Theme.h

    Dede's colour scheme and control styling, after the Mango / Spread pattern:
    everything that decides how the plugin looks lives here, not as literals
    scattered through the components.

    The identity is the spectrum itself. Region colours are sampled from a
    violet -> cyan -> amber ramp indexed by slot, so the eight regions stay
    told apart wherever they appear (the rectangle on the analyser, the border
    handles, the delay popup that belongs to them). The backdrop is that same
    ramp taken down to near-black, cold on the left and warm on the right,
    which reads as a frequency axis without competing with the trace.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "ParamIDs.h"

namespace dede::theme
{
    // The spectral ramp. Deliberately literals rather than cross-references:
    // inline variables in a header must not depend on each other's
    // initialisation order.
    inline const juce::Colour specViolet { 0xff8f6bff };
    inline const juce::Colour specCyan   { 0xff35d6d0 };
    inline const juce::Colour specAmber  { 0xffffb03a };

    /** The ramp at `t`: 0 = violet, 0.5 = cyan, 1 = amber. Anything that
        spreads across the identity samples this. */
    inline juce::Colour spectralAt (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return t < 0.5f ? specViolet.interpolatedWith (specCyan, t * 2.0f)
                        : specCyan.interpolatedWith (specAmber, (t - 0.5f) * 2.0f);
    }

    /** The accent of region slot `slot`. Spread over the ramp so neighbouring
        slots are easy to tell apart. */
    inline juce::Colour regionColour (int slot) noexcept
    {
        const float t = maxRegions > 1 ? (float) slot / (float) (maxRegions - 1) : 0.0f;
        return spectralAt (t);
    }

    inline const juce::Colour background { 0xff14131c };   // cold near-black
    inline const juce::Colour panel      { 0xff1e1d29 };
    inline const juce::Colour panelLine  { 0xff383650 };
    inline const juce::Colour plotBack   { 0xff0c0b12 };   // analyser plot area
    inline const juce::Colour grid       { 0x66565270 };
    inline const juce::Colour gridZero   { 0xcc6e6a90 };
    inline const juce::Colour text       { 0xffe2e0ec };
    inline const juce::Colour dimText    { 0xff9a97b0 };
    inline const juce::Colour topBarBg   { 0xff0d0c12 };
    inline const juce::Colour trace      { 0xff6f6c8c };   // the input spectrum

    /** The house accent: the top-bar hairline, the preset chrome, the info
        button. Kept distinct from any region colour. */
    inline const juce::Colour accent     { 0xff35d6d0 };

    inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> b)
    {
        // The spectral ramp taken to near-black: violet bottom-left through a
        // cold centre to a faint amber top-right. Saturation is pulled well
        // down; at these brightnesses full saturation reads as coloured noise
        // rather than as a tint.
        auto backdrop = [] (juce::Colour c, float brightness)
        {
            return c.withMultipliedSaturation (0.4f).withBrightness (brightness);
        };

        juce::ColourGradient grad (backdrop (specViolet, 0.10f), b.getBottomLeft(),
                                   backdrop (specAmber,  0.13f), b.getTopRight(), false);
        grad.addColour (0.5, backdrop (specCyan, 0.085f));
        g.setGradientFill (grad);
        g.fillRect (b);
    }

    /** The house knob styling: dark disc body, accent on the value arc, the
        outline and the pointer (see the fxme-juce-controls skill). */
    inline void styleKnob (juce::Slider& s, juce::Colour a)
    {
        s.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff2a2836));
        s.setColour (juce::Slider::rotarySliderOutlineColourId, a.darker (1.6f));
        s.setColour (juce::Slider::trackColourId,               a);
        s.setColour (juce::Slider::thumbColourId,               a.brighter (0.4f));
        s.setColour (juce::Slider::textBoxTextColourId,         text);
    }

    inline void styleCombo (juce::ComboBox& c, juce::Colour a)
    {
        c.setColour (juce::ComboBox::backgroundColourId, panel);
        c.setColour (juce::ComboBox::outlineColourId,    a.darker (1.2f));
        c.setColour (juce::ComboBox::textColourId,       text);
        c.setColour (juce::ComboBox::arrowColourId,      a);
    }

    // Geometry
    inline constexpr int topBarHeight   = 54;
    inline constexpr int margin         = 10;
    inline constexpr int globalRowHeight = 96;

    inline constexpr int regionColumnWidth = 180;
    inline constexpr int minEditorWidth    = 950;   // keeps the spectrum usable beside the column
    inline constexpr int defaultWidth  = 1170;
    inline constexpr int defaultHeight = 620;

} // namespace dede::theme
