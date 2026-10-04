/*
  ------------------------------------------------------------------------------
    OutputMeter.cpp

    See OutputMeter.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "OutputMeter.h"
#include "Tooltips.h"

using namespace dede;

namespace
{
    constexpr float minDb = -60.0f;
    constexpr float maxDb = 6.0f;

    constexpr int   timerHz       = 30;
    constexpr int   peakHoldTicks = timerHz;           // one second
    constexpr float rmsFallDb     = 1.0f;              // per tick: 30 dB/s
    constexpr float peakFallDb    = 0.5f;              // per tick, after the hold

    constexpr int   labelWidth = 12;
    constexpr int   barHeight  = 7;
    constexpr int   barGap     = 3;
    constexpr int   scaleGap   = 2;

    const float scaleMarks[] { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f };

    const juce::Colour overColour { 0xffff4d4d };
}

//==============================================================================
OutputMeter::OutputMeter (DedeAudioProcessor& p)
    : processor (p)
{
    setTooltip (tips::bar::meter);
    startTimerHz (timerHz);
}

OutputMeter::~OutputMeter() = default;

void OutputMeter::timerCallback()
{
    bool changed = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& c = channels[ch];

        const float rms = processor.getOutputLevelDb (ch);
        const float shownRms = juce::jmax (rms, c.rmsDb - rmsFallDb);

        const float peak = processor.takeOutputPeakDb (ch);
        float shownPeak = c.peakDb;
        if (peak >= c.peakDb)
        {
            shownPeak = peak;
            c.holdTicks = peakHoldTicks;
        }
        else if (c.holdTicks > 0)
        {
            --c.holdTicks;
        }
        else
        {
            shownPeak = juce::jmax (peak, c.peakDb - peakFallDb);
        }

        // Below the scale, nothing moves on screen.
        const auto visible = [] (float a, float b)
        {
            return std::abs (juce::jmax (a, minDb) - juce::jmax (b, minDb)) > 0.05f;
        };

        changed = changed || visible (shownRms, c.rmsDb) || visible (shownPeak, c.peakDb);
        c.rmsDb  = shownRms;
        c.peakDb = shownPeak;
    }

    if (changed)
        repaint();
}

juce::Rectangle<float> OutputMeter::barArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) labelWidth)
                                     .withHeight ((float) (2 * barHeight + barGap));
}

float OutputMeter::xFor (float db, juce::Rectangle<float> area) const noexcept
{
    const float t = (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb);
    return area.getX() + t * area.getWidth();
}

void OutputMeter::paint (juce::Graphics& g)
{
    const auto bars = barArea();
    const float zeroX = xFor (0.0f, bars);

    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& c = channels[ch];
        const auto bar = bars.withY (bars.getY() + (float) (ch * (barHeight + barGap)))
                             .withHeight ((float) barHeight);

        g.setColour (theme::dimText);
        g.drawText (ch == 0 ? "L" : "R",
                    juce::Rectangle<float> (0.0f, bar.getY() - 2.0f, (float) labelWidth, bar.getHeight() + 4.0f),
                    juce::Justification::centredLeft);

        g.setColour (theme::plotBack);
        g.fillRect (bar);

        // The RMS bar, red past 0 dBFS.
        const float rmsX = xFor (c.rmsDb, bar);
        if (rmsX > bar.getX())
        {
            g.setColour (theme::accent.withAlpha (0.85f));
            g.fillRect (bar.withRight (juce::jmin (rmsX, zeroX)));
            if (rmsX > zeroX)
            {
                g.setColour (overColour);
                g.fillRect (bar.withLeft (zeroX).withRight (rmsX));
            }
        }

        // The held peak.
        if (c.peakDb > minDb)
        {
            const float px = xFor (c.peakDb, bar);
            g.setColour (c.peakDb > 0.0f ? overColour : theme::text);
            g.fillRect (juce::Rectangle<float> (px - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
        }
    }

    // The 0 dBFS line across both bars, and the scale.
    g.setColour (theme::panelLine.brighter (0.4f));
    g.fillRect (juce::Rectangle<float> (zeroX, bars.getY(), 1.0f, bars.getHeight()));

    const float scaleY = bars.getBottom() + (float) scaleGap;
    for (float db : scaleMarks)
    {
        const float x = xFor (db, bars);
        g.setColour (theme::panelLine);
        g.fillRect (juce::Rectangle<float> (x, scaleY, 1.0f, 3.0f));
        g.setColour (theme::dimText);
        g.drawText (juce::String ((int) db),
                    juce::Rectangle<float> (x - 12.0f, scaleY + 2.0f, 24.0f, 10.0f),
                    juce::Justification::centred);
    }
}
