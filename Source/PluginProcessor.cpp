/*
  ------------------------------------------------------------------------------
    PluginProcessor.cpp

    See PluginProcessor.h for what Dede is.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace dede;

namespace
{
    /** A range whose midpoint sits at the geometric mean, i.e. a logarithmic
        feel: the same knob travel is the same frequency (or time) ratio
        everywhere. */
    juce::NormalisableRange<float> logRange (float lo, float hi, float interval = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, interval);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    /** The default band of region slot `slot`: the audible range cut into
        maxRegions equal ratios, so enabling a fresh slot never produces a
        degenerate or duplicated rectangle. */
    float defaultBandEdge (int edge)
    {
        return minFreqHz * std::pow (maxFreqHz / minFreqHz,
                                     (float) edge / (float) maxRegions);
    }

    juce::String hzToText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
    }

    /** Feedback, send and gate minima read as "-inf" rather than a number:
        that is what the DSP does with them. */
    juce::String dbToText (float v, int)
    {
        return v <= minFeedbackDb ? juce::String ("-inf dB")
                                  : juce::String (v, 1) + " dB";
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
DedeAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Global levels -----------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::dryLevel, 1 }, "Dry",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::wetLevel, 1 }, "Delays",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::outLevel, 1 }, "Output",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

    // ---- Shared reverb -----------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::revRoom, 1 }, "Room",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::revDamp, 1 }, "Damping",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::revWidth, 1 }, "Width",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::revLevel, 1 }, "Reverb",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

    // ---- Engine ----------------------------------------------------------------
    // The splitter's window: resolution against latency. Not automatable: it
    // re-allocates and changes the latency, which must not come from a lane.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::engine, 1 }, "Engine", engineNames, 0,
        AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::bandMode, 1 }, "Bands", bandModeNames, 0,
        AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::panLaw, 1 }, "Pan Law", panLawNames, 0,
        AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::fftSize, 1 }, "FFT Size", fftSizeNames, defaultFftSizeIndex,
        AudioParameterChoiceAttributes().withAutomatable (false)));

    const auto msToText = [] (float x, int) { return String (x, x < 10.0f ? 1 : 0) + " ms"; };
    const auto kneeToText = [] (float x, int) { return x <= 0.0f ? String ("hard") : String (x, 1) + " dB"; };

    // ---- Region slots ------------------------------------------------------
    const StringArray modeNames { "Seconds", "DAW sync", "MIDI note" };

    for (int i = 0; i < maxRegions; ++i)
    {
        const String slot = "R" + String (i + 1) + " ";
        const auto id = [i] (const char* suffix) { return ids::region (i, suffix); };

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgOn), 1 }, slot + "On", false));

        // Off stops the band feeding the region (its tails ring out); solo
        // silences every region not soloed. Not to be confused with "on"
        // above, which says whether the slot is in use at all.
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgActive), 1 }, slot + "Active", true));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgSolo), 1 }, slot + "Solo", false,
            AudioParameterBoolAttributes().withAutomatable (false)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFLo), 1 }, slot + "Low",
            logRange (minFreqHz, maxFreqHz), defaultBandEdge (i),
            AudioParameterFloatAttributes().withStringFromValueFunction (hzToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFHi), 1 }, slot + "High",
            logRange (minFreqHz, maxFreqHz), defaultBandEdge (i + 1),
            AudioParameterFloatAttributes().withStringFromValueFunction (hzToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgGate), 1 }, slot + "Gate",
            NormalisableRange<float> (gateFloorDb, gateCeilDb, 0.1f), gateFloorDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return x <= gateFloorDb ? String ("open")
                                                            : String (x, 1) + " dB"; })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgCeil), 1 }, slot + "Ceiling",
            NormalisableRange<float> (gateFloorDb, gateCeilDb, 0.1f), gateCeilDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return x >= gateCeilDb ? String ("off")
                                                           : String (x, 1) + " dB"; })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgGain), 1 }, slot + "Gain",
            NormalisableRange<float> (minGainDb, maxGainDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgPan), 1 }, slot + "Pan",
            NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgTimeL), 1 }, slot + "Time L",
            logRange (minDelayValue, maxDelayValue), defaultTimeL));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgTimeR), 1 }, slot + "Time R",
            logRange (minDelayValue, maxDelayValue), defaultTimeR));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id (ids::rgMode), 1 }, slot + "Mode",
            modeNames, (int) DelayMode::sync));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFbL), 1 }, slot + "Fb L",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), defaultFeedbackDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFbR), 1 }, slot + "Fb R",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), defaultFeedbackDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFbX), 1 }, slot + "Cross Fb",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), minFeedbackDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        // The region's own band, degraded and panned but not delayed, into
        // the mix, at 0 dB by default: a new region is heard as the processed
        // band itself, its repeats and reverb send under it. It adds to the
        // global dry rather than replacing that range in it.
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgDry), 1 }, slot + "Dry Level",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        // The region's repeats into the mix.
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgDly), 1 }, slot + "Delay Level",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), defaultDelayLevelDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgSend), 1 }, slot + "Reverb Send",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), defaultReverbSendDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        // ---- Dynamics around the two lines: a hard gate and a cut by default --
        {
            NormalisableRange<float> gateRatioRange (1.0f, infiniteRatio, 0.01f);
            gateRatioRange.setSkewForCentre (4.0f);
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id (ids::rgGateRatio), 1 }, slot + "Gate Ratio", gateRatioRange, infiniteRatio,
                AudioParameterFloatAttributes().withStringFromValueFunction (
                    [] (float x, int) { return x >= infiniteRatio - 0.5f ? String ("1:inf (gate)")
                                                                         : "1:" + String (x, x < 10.0f ? 1 : 0); })));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id (ids::rgGateRange), 1 }, slot + "Gate Range",
                NormalisableRange<float> (minGateRangeDb, 0.0f, 0.1f), minGateRangeDb,
                AudioParameterFloatAttributes().withStringFromValueFunction (
                    [] (float x, int) { return x <= minGateRangeDb ? String ("-inf dB") : String (x, 1) + " dB"; })));

            NormalisableRange<float> ceilRatioRange (1.0f, cutRatio, 0.01f);
            ceilRatioRange.setSkewForCentre (4.0f);
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id (ids::rgCeilRatio), 1 }, slot + "Ceiling Ratio", ceilRatioRange, cutRatio,
                AudioParameterFloatAttributes().withStringFromValueFunction (
                    [] (float x, int) { return isCeilingCut (x) ? String ("cut")
                                             : x >= infiniteRatio - 0.5f ? String ("inf:1 (limit)")
                                                                          : String (x, x < 10.0f ? 1 : 0) + ":1"; })));

            for (const auto* knee : { ids::rgGateKnee, ids::rgCeilKnee })
                layout.add (std::make_unique<AudioParameterFloat> (
                    ParameterID { id (knee), 1 }, slot + (knee == ids::rgGateKnee ? "Gate Knee" : "Ceiling Knee"),
                    NormalisableRange<float> (0.0f, maxKneeDb, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (kneeToText)));

            for (const auto* attack : { ids::rgGateAttack, ids::rgCeilAttack })
                layout.add (std::make_unique<AudioParameterFloat> (
                    ParameterID { id (attack), 1 }, slot + (attack == ids::rgGateAttack ? "Gate Attack" : "Ceiling Attack"),
                    logRange (minAttackMs, maxAttackMs, 0.01f), defaultAttackMs,
                    AudioParameterFloatAttributes().withStringFromValueFunction (msToText)));

            for (const auto* release : { ids::rgGateRelease, ids::rgCeilRelease })
                layout.add (std::make_unique<AudioParameterFloat> (
                    ParameterID { id (release), 1 }, slot + (release == ids::rgGateRelease ? "Gate Release" : "Ceiling Release"),
                    logRange (minReleaseMs, maxReleaseMs, 0.1f), defaultReleaseMs,
                    AudioParameterFloatAttributes().withStringFromValueFunction (msToText)));
        }

        // ---- Degrade: saturation -> crush -> downsample -> transpose -> freq shift --
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgSatOn), 1 }, slot + "Saturation On", false));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id (ids::rgSatModel), 1 }, slot + "Saturation Model",
            saturationModelNames, 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgSatDrive), 1 }, slot + "Saturation Drive",
            NormalisableRange<float> (0.0f, maxSatDriveDb, 0.1f), defaultSatDriveDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return "+" + String (x, 1) + " dB"; })));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgCrushOn), 1 }, slot + "Bit-crush On", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgCrushBits), 1 }, slot + "Bit-crush Bits",
            NormalisableRange<float> (minCrushBits, maxCrushBits, 0.01f), 8.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return String (x, 1) + " bits"; })));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgDsOn), 1 }, slot + "Downsample On", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgDsFactor), 1 }, slot + "Downsample Factor",
            logRange (1.0f, maxDownsample), 4.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return "x" + String (x, x < 10.0f ? 2 : 1); })));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgTransposeOn), 1 }, slot + "Transpose On", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgTransposeSt), 1 }, slot + "Transpose Semitones",
            NormalisableRange<float> (-maxTransposeSemitones, maxTransposeSemitones, 0.01f),
            defaultTransposeSemitones,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return (x > 0.0f ? "+" : "") + String (x, 2) + " st"; })));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgFreqShiftOn), 1 }, slot + "Freq Shift On", false));

        // Cubic: most of the travel for the few Hz around 0.
        NormalisableRange<float> freqShiftRange (
            -maxFreqShiftHz, maxFreqShiftHz,
            [] (float lo, float hi, float x) { const float s = 2.0f * x - 1.0f; return jmap (s * s * s, -1.0f, 1.0f, lo, hi); },
            [] (float lo, float hi, float v) { const float s = jmap (v, lo, hi, -1.0f, 1.0f);
                                               return 0.5f * (std::cbrt (s) + 1.0f); },
            [] (float, float, float v) { return std::round (v * 10.0f) / 10.0f; });
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFreqShiftHz), 1 }, slot + "Freq Shift Hz",
            freqShiftRange, defaultFreqShiftHz,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return (x > 0.0f ? "+" : "") + String (x, std::abs (x) < 100.0f ? 1 : 0) + " Hz"; })));

        // Where each stage sits: before the delay, in its loop, or after it.
        for (const auto* pos : { ids::rgSatPos, ids::rgCrushPos, ids::rgDsPos, ids::rgTransposePos,
                                 ids::rgFreqShiftPos })
        {
            const String stage = pos == ids::rgSatPos       ? "Saturation"
                               : pos == ids::rgCrushPos     ? "Bit-crush"
                               : pos == ids::rgDsPos        ? "Downsample"
                               : pos == ids::rgTransposePos ? "Transpose" : "Freq Shift";
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { id (pos), 1 }, slot + stage + " Position",
                degradePositionNames, (int) DegradePosition::before));
        }

        // ---- Spectral transforms: pitch, freeze, blur, all off ----------------
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgPitchOn), 1 }, slot + "Pitch On", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgPitchSt), 1 }, slot + "Pitch",
            NormalisableRange<float> (-maxPitchSemitones, maxPitchSemitones, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return (x > 0.0f ? "+" : "") + String (x, 2) + " st"; })));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgFreezeOn), 1 }, slot + "Freeze", false));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id (ids::rgFreezeMode), 1 }, slot + "Freeze Mode",
            freezeModeNames, 0));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id (ids::rgFreezeTrig), 1 }, slot + "Freeze Trigger",
            freezeTriggerNames, (int) FreezeTrigger::manual));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id (ids::rgFreezeRate), 1 }, slot + "Freeze Rate",
            freezeRateNames, defaultFreezeRateIndex));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFreezeWidth), 1 }, slot + "Freeze Width",
            NormalisableRange<float> (0.0f, 1.0f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return String (juce::roundToInt (x * 100.0f)) + " %"; })));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgBlurOn), 1 }, slot + "Blur On", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgBlurMs), 1 }, slot + "Blur Time",
            logRange (minBlurMs, maxBlurMs), defaultBlurMs,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float x, int) { return x >= 1000.0f ? String (x / 1000.0f, 2) + " s"
                                                        : String (juce::roundToInt (x)) + " ms"; })));
    }

    return layout;
}

//==============================================================================
DedeAudioProcessor::DedeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout()),
      presetManager (apvts,
                     fxme::PresetManager::getVendorPresetDirectory ("SpectralDede")
                   #if DEDE_HAS_BINARY_DATA
                     , BinaryData::namedResourceList,
                     BinaryData::namedResourceListSize,
                     BinaryData::getNamedResource
                   #endif
                     ),
      regionPresetLibrary (regionPresetModule, 1,
                           fxme::PresetManager::getModulePresetDirectory (regionPresetModule)
                         #if DEDE_HAS_BINARY_DATA
                           , BinaryData::namedResourceList,
                           BinaryData::namedResourceListSize,
                           BinaryData::getNamedResource
                         #endif
                           )
{
    // A region preset is everything about a region but where it is (its
    // frequency range) and whether it is in use or listened to alone.
    const auto inRegionPreset = [] (const juce::String& key)
    {
        return key != ids::rgFLo && key != ids::rgFHi && key != ids::rgOn
            && key != ids::rgActive && key != ids::rgSolo;
    };
    for (int i = 0; i < maxRegions; ++i)
        regionPresets[(size_t) i] = std::make_unique<fxme::ModulePresetTarget> (
            regionPresetLibrary, apvts, ids::region (i, ""), inRegionPreset);

    cacheParameterPointers();
    inputTap.setEnabled (true);
    splitter.setBandProcessor (&spectral);
    apvts.addParameterListener (ids::fftSize, this);
    apvts.addParameterListener (ids::engine, this);
    apvts.addParameterListener (ids::bandMode, this);
}

DedeAudioProcessor::~DedeAudioProcessor()
{
    apvts.removeParameterListener (ids::fftSize, this);
    apvts.removeParameterListener (ids::engine, this);
    apvts.removeParameterListener (ids::bandMode, this);
    cancelPendingUpdate();
}

//==============================================================================
int DedeAudioProcessor::requestedFftOrder() const
{
    return fftOrderForChoice (juce::roundToInt (pFftSize->load()));
}

bool DedeAudioProcessor::requestedLiveEngine() const
{
    return juce::roundToInt (pEngine->load()) == liveEngineIndex;
}

bool DedeAudioProcessor::requestedStereoBands() const
{
    return juce::roundToInt (pBandMode->load()) != monoBandsIndex;
}

void DedeAudioProcessor::configureEngine (bool live, int order, bool stereo, bool resetRegions)
{
    stereoBands.store (stereo);

    // A freshly prepared splitter starts from default dynamics: send every
    // region's again on the next block.
    for (auto& stale : dynamicsStale)
        stale = true;

    if (live)
    {
        // The filter bank: no FFT, so no latency, and the dry path is not
        // delayed at all.
        filterBank.prepare (sampleRate, preparedBlockSize, maxRegions, stereo ? 2 : 1);
        filterBank.setApplyPan (false);
        setDryLatency (0);
        activeFftOrder.store (order);   // the view keeps the size the combo shows
    }
    else
    {
        configureSpectralEngine (order);
    }

    // Whatever the regions held (degrade state, delay lines, freeze and blur
    // frames) was lined up with the other engine's timing. prepareToPlay
    // prepares them afresh itself.
    if (resetRegions)
        for (int i = 0; i < maxRegions; ++i)
            resetRegion (i);

    liveEngine.store (live);
}

void DedeAudioProcessor::setDryLatency (int latency)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) preparedBlockSize, 2 };
    dryDelay.setMaximumDelayInSamples (juce::jmax (1, latency));
    dryDelay.prepare (spec);
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();
    dryLatency = latency;
    setLatencySamples (latency);
}

void DedeAudioProcessor::configureSpectralEngine (int order)
{
    splitter.prepare (sampleRate, preparedBlockSize, maxRegions, order, stereoBands.load() ? 2 : 1);
    for (auto& stale : dynamicsStale)
        stale = true;

    // Each band leaves the splitter mono (gain applied, no pan) so the degrade
    // chain treats both sides alike; the pan is applied after it.
    splitter.setApplyPan (false);

    // Pitch, freeze and blur run inside the splitter's frames, on its window
    // and hop, so they are prepared from it.
    spectral.prepare (splitter.getFftSize(), splitter.getHopSize(), sampleRate, maxRegions);

    // The dry path waits for the splitter, so the two stay lined up, and the
    // host is told the total.
    setDryLatency (splitter.getLatencySamples());

    // Orders 10 to 14 are inside the splitter's own range (8 to 14), so it
    // runs at exactly the one asked for.
    activeFftOrder.store (order);
}

void DedeAudioProcessor::parameterChanged (const juce::String& parameterID, float)
{
    // Can come from any thread, the audio thread included: only schedule.
    if (parameterID == ids::fftSize || parameterID == ids::engine || parameterID == ids::bandMode)
        triggerAsyncUpdate();
}

void DedeAudioProcessor::handleAsyncUpdate()
{
    // Before prepareToPlay there is nothing to re-prepare; it reads the
    // parameter itself.
    if (preparedBlockSize <= 0)
        return;

    const bool live = requestedLiveEngine();
    const int order = requestedFftOrder();
    const bool stereo = requestedStereoBands();
    const bool engineChanged = live != liveEngine.load() || stereo != stereoBands.load();

    // The FFT size only matters to the spectral engine; in live mode it is
    // applied when switching back.
    if (! engineChanged && (live || order == activeFftOrder.load()))
    {
        activeFftOrder.store (order);
        return;
    }

    // suspendProcessing takes the callback lock, so no block is in flight
    // while the engine re-allocates; the host gets silence meanwhile (about
    // one window). The GUI reads the splitters from this same thread.
    suspendProcessing (true);
    if (engineChanged)
        configureEngine (live, order, stereo, true);
    else
        configureSpectralEngine (order);
    suspendProcessing (false);
}

void DedeAudioProcessor::cacheParameterPointers()
{
    const auto raw = [this] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    pDryLevel = raw (ids::dryLevel);
    pWetLevel = raw (ids::wetLevel);
    pOutLevel = raw (ids::outLevel);
    pRevRoom  = raw (ids::revRoom);
    pRevDamp  = raw (ids::revDamp);
    pRevWidth = raw (ids::revWidth);
    pRevLevel = raw (ids::revLevel);
    pFftSize  = raw (ids::fftSize);
    pEngine   = raw (ids::engine);
    pBandMode = raw (ids::bandMode);
    pPanLaw   = raw (ids::panLaw);

    for (int i = 0; i < maxRegions; ++i)
    {
        auto& r = regions[i];
        r.on    = raw (ids::region (i, ids::rgOn));
        r.active = raw (ids::region (i, ids::rgActive));
        r.solo  = raw (ids::region (i, ids::rgSolo));
        r.fLo   = raw (ids::region (i, ids::rgFLo));
        r.fHi   = raw (ids::region (i, ids::rgFHi));
        r.gate  = raw (ids::region (i, ids::rgGate));
        r.ceil  = raw (ids::region (i, ids::rgCeil));
        r.gain  = raw (ids::region (i, ids::rgGain));
        r.pan   = raw (ids::region (i, ids::rgPan));
        r.timeL = raw (ids::region (i, ids::rgTimeL));
        r.timeR = raw (ids::region (i, ids::rgTimeR));
        r.mode  = raw (ids::region (i, ids::rgMode));
        r.fbL   = raw (ids::region (i, ids::rgFbL));
        r.fbR   = raw (ids::region (i, ids::rgFbR));
        r.fbX   = raw (ids::region (i, ids::rgFbX));
        r.dry   = raw (ids::region (i, ids::rgDry));
        r.dly   = raw (ids::region (i, ids::rgDly));
        r.send  = raw (ids::region (i, ids::rgSend));

        r.satOn    = raw (ids::region (i, ids::rgSatOn));
        r.satModel = raw (ids::region (i, ids::rgSatModel));
        r.satDrive = raw (ids::region (i, ids::rgSatDrive));
        r.crushOn  = raw (ids::region (i, ids::rgCrushOn));
        r.crushBits = raw (ids::region (i, ids::rgCrushBits));
        r.dsOn     = raw (ids::region (i, ids::rgDsOn));
        r.dsFactor = raw (ids::region (i, ids::rgDsFactor));
        r.gateRatio   = raw (ids::region (i, ids::rgGateRatio));
        r.gateRange   = raw (ids::region (i, ids::rgGateRange));
        r.gateKnee    = raw (ids::region (i, ids::rgGateKnee));
        r.gateAttack  = raw (ids::region (i, ids::rgGateAttack));
        r.gateRelease = raw (ids::region (i, ids::rgGateRelease));
        r.ceilRatio   = raw (ids::region (i, ids::rgCeilRatio));
        r.ceilKnee    = raw (ids::region (i, ids::rgCeilKnee));
        r.ceilAttack  = raw (ids::region (i, ids::rgCeilAttack));
        r.ceilRelease = raw (ids::region (i, ids::rgCeilRelease));

        r.transposeOn  = raw (ids::region (i, ids::rgTransposeOn));
        r.transposeSt  = raw (ids::region (i, ids::rgTransposeSt));
        r.satPos   = raw (ids::region (i, ids::rgSatPos));
        r.crushPos = raw (ids::region (i, ids::rgCrushPos));
        r.dsPos    = raw (ids::region (i, ids::rgDsPos));
        r.transposePos = raw (ids::region (i, ids::rgTransposePos));
        r.freqShiftOn  = raw (ids::region (i, ids::rgFreqShiftOn));
        r.freqShiftHz  = raw (ids::region (i, ids::rgFreqShiftHz));
        r.freqShiftPos = raw (ids::region (i, ids::rgFreqShiftPos));

        r.pitchOn    = raw (ids::region (i, ids::rgPitchOn));
        r.pitchSt    = raw (ids::region (i, ids::rgPitchSt));
        r.freezeOn   = raw (ids::region (i, ids::rgFreezeOn));
        r.freezeMode = raw (ids::region (i, ids::rgFreezeMode));
        r.freezeTrig = raw (ids::region (i, ids::rgFreezeTrig));
        r.freezeRate = raw (ids::region (i, ids::rgFreezeRate));
        r.freezeWidth = raw (ids::region (i, ids::rgFreezeWidth));
        r.blurOn     = raw (ids::region (i, ids::rgBlurOn));
        r.blurMs     = raw (ids::region (i, ids::rgBlurMs));
    }
}

//==============================================================================
void DedeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    preparedBlockSize = samplesPerBlock;
    std::fill (std::begin (freezeStep), std::end (freezeStep), noFreezeStep);

    configureEngine (requestedLiveEngine(), requestedFftOrder(), requestedStereoBands(), false);

    for (auto& d : delays)
        d.prepare (sampleRate, maxDelaySeconds);

    reverb.prepare (sampleRate, samplesPerBlock);
    reverb.setRoomSize (pRevRoom->load());
    reverb.setDamping (pRevDamp->load());
    reverb.setWidth (pRevWidth->load());

    monoBuffer.setSize (1, samplesPerBlock, false, false, true);
    regionBuffer.setSize (2, samplesPerBlock, false, false, true);
    wetBuffer.setSize (2, samplesPerBlock, false, false, true);
    sendBuffer.setSize (2, samplesPerBlock, false, false, true);
    meterBuffer.setSize (2, samplesPerBlock, false, false, true);
    meterBuffer.clear();
    directBuffer.setSize (2, samplesPerBlock, false, false, true);
    directBuffer.clear();
    bandBuffer.setSize (2, samplesPerBlock, false, false, true);
    panBuffer.setSize (2, samplesPerBlock, false, false, true);
    bandBuffer.clear();
    panBuffer.clear();
    monoBuffer.clear();
    regionBuffer.clear();
    wetBuffer.clear();
    sendBuffer.clear();

    dryGain.reset (sampleRate, 0.02);
    wetGain.reset (sampleRate, 0.02);
    revGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    for (auto& m : outputMeters)
        m.prepare (sampleRate);
    dryGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load()));
    wetGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pWetLevel->load()));
    revGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pRevLevel->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));

    for (int i = 0; i < maxRegions; ++i)
    {
        sendGain[i].reset (sampleRate, 0.02);
        sendGain[i].setCurrentAndTargetValue (sendLevel (i));
        delayGain[i].reset (sampleRate, 0.02);
        delayGain[i].setCurrentAndTargetValue (delayLevel (i));
        regionDryGain[i].reset (sampleRate, 0.02);
        regionDryGain[i].setCurrentAndTargetValue (regionDryLevel (i));

        for (auto& g : pan[i])
            g.reset (sampleRate, 0.02);
        snapPan (i);

        auto& d = degrade[i];
        for (int ch = 0; ch < 2; ++ch)
        {
            d.sat[ch].prepare (sampleRate);
            d.satLevel[ch].prepare (sampleRate);
            d.ds[ch].reset();
        }
        d.transpose.prepare (sampleRate, 2);
        d.freqShift.prepare (sampleRate, 2);
        d.satDrive.reset (sampleRate, 0.02);
        d.satPos = d.crushPos = d.dsPos = d.transposePos = d.freqShiftPos = -1;

        audibleGain[i].reset (sampleRate, 0.005);
        audibleGain[i].setCurrentAndTargetValue (isAudible (i, anySolo()) ? 1.0f : 0.0f);
        needsReset[i] = false;
        for (auto& m : meters[i])
            m.prepare (sampleRate);
    }
}

void DedeAudioProcessor::releaseResources()
{
    splitter.reset();
    spectral.reset();
    dryDelay.reset();
    reverb.reset();
    for (auto& d : delays)
        d.reset();
    for (int i = 0; i < maxRegions; ++i)
        resetDegrade (i);
}

void DedeAudioProcessor::resetDegrade (int slot)
{
    auto& d = degrade[slot];
    for (int ch = 0; ch < 2; ++ch)
    {
        d.sat[ch].reset();
        d.satLevel[ch].reset();
        d.ds[ch].reset();
    }
    d.transpose.reset();
    d.freqShift.reset();

    // Every stage counts as newly placed on the next block, so it also snaps
    // its drive rather than gliding from the last region's.
    d.satPos = d.crushPos = d.dsPos = d.transposePos = d.freqShiftPos = -1;
}

bool DedeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Mono into stereo is allowed (the regions pan it); anything else has to
    // match the output.
    return in == out || in == juce::AudioChannelSet::mono();
}

//==============================================================================
void DedeAudioProcessor::updateTransportAndMidi (const juce::MidiBuffer& midi)
{
    hostPpq.reset();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                currentBpm.store (*bpm > 0.0 ? *bpm : 120.0);
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                    hostPpq = *ppq;
        }

    // The *last* note of the block wins: MIDI-note mode tracks whatever was
    // played most recently, and re-triggering the same note is a no-op.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            lastNoteHz.store ((float) m.getMidiNoteInHertz (m.getNoteNumber()));
    }
}

void DedeAudioProcessor::buildMonoInput (const juce::AudioBuffer<float>& buffer, int startSample,
                                         int numSamples)
{
    const int numCh  = juce::jmax (1, buffer.getNumChannels());
    const float norm = 1.0f / (float) numCh;

    float* mono = monoBuffer.getWritePointer (0);
    juce::FloatVectorOperations::clear (mono, numSamples);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::addWithMultiply (
            mono, buffer.getReadPointer (ch, startSample), norm, numSamples);
}

void DedeAudioProcessor::updateBands()
{
    const bool live = liveEngine.load();

    for (int i = 0; i < maxRegions; ++i)
    {
        const auto& r = regions[i];

        // A region whose borders have been dragged past each other is still a
        // valid region: take the interval either way round.
        const float a = r.fLo->load(), b = r.fHi->load();
        const float gateDb = r.gate->load();
        const float ceilDb = r.ceil->load();

        fxme::SpectralBand band;
        // A region switched off keeps its slot and its delay; only its band
        // stops feeding it, so its tails ring out.
        band.enabled = r.on->load() > 0.5f && r.active->load() > 0.5f;
        band.lowHz   = juce::jmin (a, b);
        band.highHz  = juce::jmax (a, b);
        band.gateDb  = gateDb <= gateFloorDb ? fxme::SpectralBandSplitter::openGateDb
                                             : gateDb;
        band.gainDb  = r.gain->load();
        band.pan     = r.pan->load();

        // The ends of the range switch the stage off rather than meaning a
        // level: the gate at its floor is open, the ceiling at its top is off.
        band.ceilingDb = ceilDb >= gateCeilDb ? fxme::SpectralBandSplitter::offCeilingDb
                                              : ceilDb;

        if (live)
            filterBank.setBand (i, band);
        else
            splitter.setBand (i, band);

        // The dynamics around the lines, passed on only when they change (a
        // change rebuilds the band's curve).
        const auto dyn = regionDynamics (i);
        if (! sameDynamics (dyn, sentDynamics[i]) || dynamicsStale[i])
        {
            if (live)
                filterBank.setBandDynamics (i, dyn);
            else
                splitter.setBandDynamics (i, dyn);
            sentDynamics[i] = dyn;
            dynamicsStale[i] = false;
        }

        fxme::SpectralBandEffects::Settings fx;
        fx.pitchOn        = r.pitchOn->load() > 0.5f;
        fx.pitchSemitones = r.pitchSt->load();
        fx.freezeOn       = r.freezeOn->load() > 0.5f;
        fx.freezeMode     = (fxme::SpectralBandEffects::FreezeMode)
                                juce::jlimit (0, 1, (int) r.freezeMode->load());
        fx.freezeWidth    = r.freezeWidth->load();
        // A gated freeze keeps the analysis going between its steps, so each
        // capture lands on the step, in tune, instead of one hop late.
        fx.keepTracking   = fx.freezeOn && freezeTrigger (i) == FreezeTrigger::onOff;
        fx.blurOn         = r.blurOn->load() > 0.5f;
        fx.blurSeconds    = r.blurMs->load() * 0.001f;
        bandEffects[i] = fx;
        spectral.setBand (i, fx);
    }
}

FreezeTrigger DedeAudioProcessor::freezeTrigger (int slot) const noexcept
{
    return (FreezeTrigger) juce::jlimit (0, freezeTriggerNames.size() - 1,
                                         (int) regions[slot].freezeTrig->load());
}

int DedeAudioProcessor::runFreezeClock (double ppq, double quartersPerSample)
{
    int untilNext = std::numeric_limits<int>::max();

    for (int i = 0; i < maxRegions; ++i)
    {
        const auto trigger = freezeTrigger (i);
        auto& fx = bandEffects[i];

        if (regions[i].freezeOn->load() <= 0.5f || trigger == FreezeTrigger::manual)
        {
            freezeStep[i] = noFreezeStep;
            continue;
        }

        // The step this sample falls in. Half a sample of slack, so a chunk
        // that starts on a boundary (up to rounding) is counted in the step
        // it starts rather than leaving a one-sample chunk behind it.
        const double step = freezeRateQuarters ((int) regions[i].freezeRate->load());
        const auto index = (int64_t) std::floor ((ppq + 0.5 * quartersPerSample) / step);

        if (trigger == FreezeTrigger::retrigger)
        {
            if (freezeStep[i] != noFreezeStep && index != freezeStep[i])
                spectral.retriggerFreeze (i);
        }
        else
        {
            fx.freezeOn = (index & 1) == 0;
            spectral.setBand (i, fx);
        }
        freezeStep[i] = index;

        const double toBoundary = ((double) (index + 1) * step - ppq) / quartersPerSample - 0.5;
        untilNext = juce::jmin (untilNext, juce::jmax (1, (int) std::ceil (toBoundary)));
    }

    return untilNext;
}

fxme::BandDynamics DedeAudioProcessor::regionDynamics (int slot) const
{
    const auto& r = regions[slot];
    fxme::BandDynamics d;

    d.gateRatio   = ratioFromParameter (r.gateRatio->load());
    const float range = r.gateRange->load();
    d.gateRangeDb = range <= minGateRangeDb ? -fxme::BandDynamics::infinity : range;
    d.gateKneeDb  = r.gateKnee->load();
    d.gateAttackSeconds  = r.gateAttack->load()  * 0.001f;
    d.gateReleaseSeconds = r.gateRelease->load() * 0.001f;

    const float ceilRatio = r.ceilRatio->load();
    d.ceilingCut   = isCeilingCut (ceilRatio);
    d.ceilingRatio = ratioFromParameter (ceilRatio);
    d.ceilingKneeDb = r.ceilKnee->load();
    d.ceilingAttackSeconds  = r.ceilAttack->load()  * 0.001f;
    d.ceilingReleaseSeconds = r.ceilRelease->load() * 0.001f;
    return d;
}

bool DedeAudioProcessor::sameDynamics (const fxme::BandDynamics& a, const fxme::BandDynamics& b) noexcept
{
    return a.gateRatio == b.gateRatio && a.gateRangeDb == b.gateRangeDb && a.gateKneeDb == b.gateKneeDb
        && a.gateAttackSeconds == b.gateAttackSeconds && a.gateReleaseSeconds == b.gateReleaseSeconds
        && a.ceilingCut == b.ceilingCut && a.ceilingRatio == b.ceilingRatio
        && a.ceilingKneeDb == b.ceilingKneeDb && a.ceilingAttackSeconds == b.ceilingAttackSeconds
        && a.ceilingReleaseSeconds == b.ceilingReleaseSeconds;
}

float DedeAudioProcessor::sendLevel (int slot) const
{
    const float db = regions[slot].send->load();
    return db <= minFeedbackDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}

float DedeAudioProcessor::delayLevel (int slot) const
{
    const float db = regions[slot].dly->load();
    return db <= minFeedbackDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}

float DedeAudioProcessor::regionDryLevel (int slot) const
{
    const float db = regions[slot].dry->load();
    return db <= minFeedbackDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}

void DedeAudioProcessor::panMatrix (int slot, float (&m)[4]) const
{
    const float p = juce::jlimit (-1.0f, 1.0f, regions[slot].pan->load());
    m[panLL] = m[panRL] = m[panLR] = m[panRR] = 0.0f;

    // A mono band (fed to both inputs of the matrix): constant power, so
    // sweeping a region across the image keeps its loudness.
    if (! stereoBands.load())
    {
        const float theta = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        m[panLL] = std::cos (theta);
        m[panRR] = std::sin (theta);
        return;
    }

    // A stereo band: centred, it is left as it is. Moving away from a side
    // turns that side down (cos of up to a quarter turn); a stereo pan also
    // folds it into the other side (sin), so its content is not lost.
    const float theta = std::abs (p) * juce::MathConstants<float>::pi * 0.5f;
    const bool fold = juce::roundToInt (pPanLaw->load()) == stereoPanIndex;
    m[panLL] = m[panRR] = 1.0f;
    if (p > 0.0f)
    {
        m[panLL] = std::cos (theta);
        m[panLR] = fold ? std::sin (theta) : 0.0f;
    }
    else if (p < 0.0f)
    {
        m[panRR] = std::cos (theta);
        m[panRL] = fold ? std::sin (theta) : 0.0f;
    }
}

void DedeAudioProcessor::snapPan (int slot)
{
    float m[4];
    panMatrix (slot, m);
    for (int k = 0; k < 4; ++k)
        pan[slot][k].setCurrentAndTargetValue (m[k]);
}

bool DedeAudioProcessor::anySolo() const noexcept
{
    for (const auto& r : regions)
        if (r.on->load() > 0.5f && r.solo->load() > 0.5f)
            return true;
    return false;
}

bool DedeAudioProcessor::isAudible (int slot, bool soloing) const noexcept
{
    const auto& r = regions[slot];
    return r.on->load() > 0.5f
        && (! soloing || r.solo->load() > 0.5f);
}

void DedeAudioProcessor::resetRegion (int slot)
{
    delays[slot].reset();
    resetDegrade (slot);
    spectral.resetBand (slot);
    for (auto& m : meters[slot])
        m.clear();

    // Levels and pan start where the new region's parameters are, rather than
    // gliding from wherever the previous one left them.
    snapPan (slot);
    delayGain[slot].setCurrentAndTargetValue (delayLevel (slot));
    regionDryGain[slot].setCurrentAndTargetValue (regionDryLevel (slot));
    sendGain[slot].setCurrentAndTargetValue (sendLevel (slot));
}

float DedeAudioProcessor::getRegionLevelDb (int slot, int channel) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, maxRegions) || ! juce::isPositiveAndBelow (channel, 2))
        return -100.0f;
    return meters[slot][channel].getRMS();
}

float DedeAudioProcessor::getOutputLevelDb (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, 2) ? outputMeters[channel].getRMS() : -100.0f;
}

float DedeAudioProcessor::takeOutputPeakDb (int channel) noexcept
{
    if (! juce::isPositiveAndBelow (channel, 2))
        return -100.0f;

    // Reset after reading, so no peak between two GUI frames is missed. A
    // peak landing between the two calls can be lost; harmless for a meter.
    const float db = outputMeters[channel].getMax();
    outputMeters[channel].resetMax();
    return db;
}

float DedeAudioProcessor::getGateOpenness (int slot) const noexcept
{
    return liveEngine.load() ? filterBank.getGateOpenness (slot)
                             : splitter.getGateOpenness (slot);
}

float DedeAudioProcessor::getBandLevelDb (int slot) const noexcept
{
    return liveEngine.load() ? filterBank.getBandLevelDb (slot) : -200.0f;
}

float DedeAudioProcessor::takeBandPeakLevelDb (int slot) noexcept
{
    return liveEngine.load() ? filterBank.takeBandPeakLevelDb (slot) : -200.0f;
}

void DedeAudioProcessor::beginDegradeBlock (int slot)
{
    const auto& r = regions[slot];
    auto& d = degrade[slot];

    // Where a stage runs this block, -1 when off. A stage that has just come
    // on, or moved, starts from rest: its state belonged to another signal.
    const auto place = [] (std::atomic<float>* on, std::atomic<float>* pos)
    {
        return on->load() > 0.5f ? juce::jlimit (0, 2, (int) pos->load()) : -1;
    };

    const int satPos = place (r.satOn, r.satPos);
    if (satPos >= 0)
    {
        const float driveGain = juce::Decibels::decibelsToGain (r.satDrive->load());
        if (satPos != d.satPos)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                d.sat[ch].reset();
                d.satLevel[ch].reset();
            }
            d.satDrive.setCurrentAndTargetValue (driveGain);
        }

        // Bias and sag are not parameters; fixed values give each model its
        // character. Sag makes Dynamic differ from Standard at all, and a
        // negative overlap is what gives Class AB its crossover notch.
        using Model = fxme::Saturator::Model;
        const auto model = (Model) juce::jlimit (0, 3, (int) r.satModel->load());
        for (auto& sat : d.sat)
        {
            sat.setModel (model);
            sat.setSag (model == Model::Standard ? 0.0f : 0.5f);
            sat.setBias (model == Model::ClassAB ? -0.1f : 0.0f);
        }
        d.satDrive.setTargetValue (driveGain);
    }
    d.satPos = satPos;

    d.crushPos = place (r.crushOn, r.crushPos);
    if (d.crushPos >= 0)
        for (auto& c : d.crush)
            c.setBits (r.crushBits->load());

    const int dsPos = place (r.dsOn, r.dsPos);
    if (dsPos >= 0)
    {
        if (dsPos != d.dsPos)
            for (auto& ds : d.ds)
                ds.reset();
        for (auto& ds : d.ds)
            ds.setFactor (r.dsFactor->load());
    }
    d.dsPos = dsPos;

    const int transposePos = place (r.transposeOn, r.transposePos);
    if (transposePos >= 0)
    {
        d.transpose.setPitchSemitones (r.transposeSt->load());
        if (transposePos != d.transposePos)
            d.transpose.reset();
    }
    d.transposePos = transposePos;

    const int freqShiftPos = place (r.freqShiftOn, r.freqShiftPos);
    if (freqShiftPos >= 0)
    {
        d.freqShift.setShiftHz (r.freqShiftHz->load());
        if (freqShiftPos != d.freqShiftPos)
            d.freqShift.reset();
    }
    d.freqShiftPos = freqShiftPos;
}

bool DedeAudioProcessor::hasDegradeAt (int slot, DegradePosition position) const noexcept
{
    const auto& d = degrade[slot];
    const int p = (int) position;
    return d.satPos == p || d.crushPos == p || d.dsPos == p || d.transposePos == p
        || d.freqShiftPos == p;
}

void DedeAudioProcessor::degradeFrame (int slot, DegradePosition position,
                                       float* frame, int numChannels) noexcept
{
    auto& d = degrade[slot];
    const int p = (int) position;

    if (d.satPos == p)
    {
        // The drive sets how hard the band clips, not how loud it comes out.
        // Dividing back by the drive is right for small signals only: a band
        // that clips is capped whatever the drive, so the division alone
        // makes it quieter as the drive goes up. The level matcher brings
        // the result back to the RMS before the saturator, measured (200 ms
        // followers), which holds at every level.
        //
        // In stereo the two sides are linked, so the image does not move with
        // the level: both supply rails sag with the louder side, and one
        // auto-gain follows both sides' power.
        const float g = d.satDrive.getNextValue();
        if (numChannels == 1)
        {
            d.sat[0].setDriveGain (g);
            frame[0] = d.satLevel[0].process (frame[0], d.sat[0].processSample (frame[0]) / g);
        }
        else
        {
            const float reference[2] { frame[0], frame[1] };
            const float demand = std::abs (frame[0]) > std::abs (frame[1]) ? frame[0] : frame[1];
            for (int ch = 0; ch < 2; ++ch)
            {
                d.sat[ch].setDriveGain (g);
                frame[ch] = d.sat[ch].processSample (frame[ch], demand) / g;
            }
            d.satLevel[0].processFrame (reference, frame, 2);
        }
    }

    if (d.crushPos == p)
        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] = d.crush[ch].processSample (frame[ch]);

    if (d.dsPos == p)
        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] = d.ds[ch].processSample (frame[ch]);

    if (d.transposePos == p)
    {
        // The shifter is stereo; a mono frame runs with a silent right side.
        float f[2] { frame[0], numChannels > 1 ? frame[1] : 0.0f };
        d.transpose.processFrame (f);
        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] = f[ch];
    }

    if (d.freqShiftPos == p)
    {
        // Stereo too, one oscillator for both sides; a mono frame likewise.
        float f[2] { frame[0], numChannels > 1 ? frame[1] : 0.0f };
        d.freqShift.processFrame (f);
        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] = f[ch];
    }
}

void DedeAudioProcessor::updateDelay (int slot)
{
    const auto& r = regions[slot];

    fxme::DelayTimeResolver resolver;
    resolver.bpm        = currentBpm.load();
    resolver.noteHz     = lastNoteHz.load();
    resolver.maxSeconds = maxDelaySeconds;

    const auto mode = (fxme::DelayTimeMode) juce::jlimit (0, numDelayModes - 1,
                                                          (int) r.mode->load());

    // A Transpose in the loop delays everything going round by its own latency:
    // take it off the lines, so the repeats still land on time.
    float loopExtra = 0.0f;
    if (r.transposeOn->load() > 0.5f && (int) r.transposePos->load() == (int) DegradePosition::loop)
        loopExtra = degrade[slot].transpose.getLatencySamples() / (float) sampleRate;

    const auto seconds = [&] (float value)
    {
        return juce::jmax (minLoopSeconds, resolver.toSeconds (value, mode) - loopExtra);
    };
    delays[slot].setDelaySeconds (seconds (r.timeL->load()), seconds (r.timeR->load()));

    // The lowest point of a feedback range is silence, not -60 dB: a delay set
    // to no feedback has to be exactly one repeat.
    const auto feedbackGain = [] (float db)
    {
        return db <= minFeedbackDb ? 0.0f : juce::Decibels::decibelsToGain (db);
    };

    delays[slot].setFeedback (feedbackGain (r.fbL->load()),
                              feedbackGain (r.fbR->load()),
                              feedbackGain (r.fbX->load()));
}

void DedeAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample,
                                       int numSamples)
{
    const int numOut = getTotalNumOutputChannels();

    // The spectrum view shows the mono sum, and every decision in the
    // splitters (gates, ceilings, the spectral effects' analysis) is taken on
    // that same sum, so the lines drawn on the trace mean what they look
    // like. With stereo bands the splitters get both channels and keep them;
    // with mono bands they get the sum itself.
    buildMonoInput (buffer, startSample, numSamples);
    inputTap.push (monoBuffer.getReadPointer (0), numSamples);

    const bool live = liveEngine.load();
    const bool stereo = stereoBands.load();
    const float* inL = buffer.getReadPointer (0, startSample);
    const float* inR = buffer.getReadPointer (juce::jmin (1, buffer.getNumChannels() - 1), startSample);

    if (stereo)
    {
        if (live)
            filterBank.process (inL, inR, numSamples);
        else
            splitter.process (inL, inR, numSamples);
    }
    else
    {
        if (live)
            filterBank.process (monoBuffer.getReadPointer (0), numSamples);
        else
            splitter.process (monoBuffer.getReadPointer (0), numSamples);
    }

    // Each region's band goes through its own stereo delay, and the delay
    // outputs are what the wet bus carries. The region's own band, undelayed,
    // is already in the untouched dry path; its Dry level adds the processed
    // version on top, on a bus of its own (directBuffer).
    wetBuffer.clear (0, numSamples);
    sendBuffer.clear (0, numSamples);
    directBuffer.clear (0, numSamples);

    const bool soloing = anySolo();

    for (int i = 0; i < maxRegions; ++i)
    {
        const bool inUse = regions[i].on->load() > 0.5f;

        // Solo, and a slot being freed, both come down to the
        // region's output gain, ramped so none of them clicks.
        audibleGain[i].setTargetValue (isAudible (i, soloing) ? 1.0f : 0.0f);

        if (! inUse)
        {
            // A freed slot plays out its fade, then stops; its state is
            // cleared when it is used again, so a new region never starts by
            // replaying the tail of the one before.
            if (! audibleGain[i].isSmoothing())
            {
                if (! needsReset[i])
                {
                    needsReset[i] = true;
                    for (auto& m : meters[i])
                        m.clear();
                }
                continue;
            }
        }
        else if (needsReset[i])
        {
            resetRegion (i);
            needsReset[i] = false;
        }

        updateDelay (i);

        // Band (gain applied; mono, or stereo) -> degrade -> pan -> the
        // region's delay.
        const int bandChannels = stereo ? 2 : 1;
        float* bandL = bandBuffer.getWritePointer (0);
        float* bandR = bandBuffer.getWritePointer (1);
        for (int ch = 0; ch < bandChannels; ++ch)
            juce::FloatVectorOperations::copy (ch == 0 ? bandL : bandR,
                                               live ? filterBank.getBandOutput (i, ch)
                                                    : splitter.getBandOutput (i, ch),
                                               numSamples);

        // The degrade stages placed before the delay work on the band, so the
        // region's Dry hears them too.
        beginDegradeBlock (i);
        if (hasDegradeAt (i, DegradePosition::before))
            for (int s = 0; s < numSamples; ++s)
            {
                float frame[2] { bandL[s], stereo ? bandR[s] : 0.0f };
                degradeFrame (i, DegradePosition::before, frame, bandChannels);
                bandL[s] = frame[0];
                if (stereo)
                    bandR[s] = frame[1];
            }

        // The pan as a 2 x 2 matrix: a mono band feeds both of its inputs.
        float m[4];
        panMatrix (i, m);
        for (int k = 0; k < 4; ++k)
            pan[i][k].setTargetValue (m[k]);

        const float* srcR = stereo ? bandR : bandL;
        float* pl = panBuffer.getWritePointer (0);
        float* pr = panBuffer.getWritePointer (1);
        for (int s = 0; s < numSamples; ++s)
        {
            const float a = bandL[s], b = srcR[s];
            const float ll = pan[i][panLL].getNextValue(), rl = pan[i][panRL].getNextValue();
            const float lr = pan[i][panLR].getNextValue(), rr = pan[i][panRR].getNextValue();
            pl[s] = a * ll + b * rl;
            pr[s] = a * lr + b * rr;
        }

        // Stages in the loop act on what enters the delay lines: the first
        // echo, then again on every repeat. Stages after it act on the
        // repeats as they leave, stereo.
        float* outL = regionBuffer.getWritePointer (0);
        float* outR = regionBuffer.getWritePointer (1);
        if (hasDegradeAt (i, DegradePosition::loop))
            delays[i].process (pl, pr, outL, outR, numSamples, [this, i] (float& l, float& r) noexcept
            {
                float frame[2] { l, r };
                degradeFrame (i, DegradePosition::loop, frame, 2);
                l = frame[0];
                r = frame[1];
            });
        else
            delays[i].process (pl, pr, outL, outR, numSamples);

        if (hasDegradeAt (i, DegradePosition::after))
            for (int s = 0; s < numSamples; ++s)
            {
                float frame[2] { outL[s], outR[s] };
                degradeFrame (i, DegradePosition::after, frame, 2);
                outL[s] = frame[0];
                outR[s] = frame[1];
            }

        // The region's output is two parts, each at its own level: its band
        // as it is (Dry) and its repeats (Delay). The reverb send is taken
        // from that output, after both levels, like a post-fader send: with
        // Delay at -inf the reverb gets no repeats from this region, with Dry
        // at -inf none of its band, and with both down it gets nothing.
        regionDryGain[i].setTargetValue (regionDryLevel (i));
        delayGain[i].setTargetValue (delayLevel (i));
        sendGain[i].setTargetValue (sendLevel (i));

        auto* wetL  = wetBuffer.getWritePointer (0);
        auto* wetR  = wetBuffer.getWritePointer (1);
        auto* metL  = meterBuffer.getWritePointer (0);
        auto* metR  = meterBuffer.getWritePointer (1);
        auto* dirL  = directBuffer.getWritePointer (0);
        auto* dirR  = directBuffer.getWritePointer (1);
        auto* sendL = sendBuffer.getWritePointer (0);
        auto* sendR = sendBuffer.getWritePointer (1);
        const auto* regL = regionBuffer.getReadPointer (0);
        const auto* regR = regionBuffer.getReadPointer (1);

        for (int s = 0; s < numSamples; ++s)
        {
            const float dry   = regionDryGain[i].getNextValue();
            const float delay = delayGain[i].getNextValue();
            const float send  = sendGain[i].getNextValue();

            const float audible = audibleGain[i].getNextValue();

            // pl/pr still hold the panned band: the delay wrote elsewhere.
            const float directL  = pl[s] * dry,     directR  = pr[s] * dry;
            const float delayedL = regL[s] * delay, delayedR = regR[s] * delay;

            // The meter reads the region's output before the solo gain, so a
            // region silenced by another's solo still shows what it would add.
            metL[s] = directL + delayedL;
            metR[s] = directR + delayedR;

            dirL[s]  += directL * audible;
            dirR[s]  += directR * audible;
            wetL[s]  += delayedL * audible;
            wetR[s]  += delayedR * audible;
            sendL[s] += (directL + delayedL) * audible * send;
            sendR[s] += (directR + delayedR) * audible * send;
        }

        meters[i][0].process (metL, numSamples);
        meters[i][1].process (metR, numSamples);
    }

    // The reverb runs every block whatever the sends are doing, so a tail keeps
    // ringing after the region that fed it has been turned down or deleted.
    reverb.setRoomSize (pRevRoom->load());
    reverb.setDamping (pRevDamp->load());
    reverb.setWidth (pRevWidth->load());
    reverb.process (sendBuffer.getReadPointer (0), sendBuffer.getReadPointer (1),
                    sendBuffer.getWritePointer (0), sendBuffer.getWritePointer (1),
                    numSamples);

    const float* wetL = wetBuffer.getReadPointer (0);
    const float* wetR = wetBuffer.getReadPointer (1);
    const float* revL = sendBuffer.getReadPointer (0);
    const float* revR = sendBuffer.getReadPointer (1);
    const float* dirL = directBuffer.getReadPointer (0);
    const float* dirR = directBuffer.getReadPointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float dg = dryGain.getNextValue();
        const float wg = wetGain.getNextValue();
        const float rg = revGain.getNextValue();
        const float og = outGain.getNextValue();
        const float wet[2] { wetL[i], wetR[i] };
        const float rev[2] { revL[i], revR[i] };
        const float dir[2] { dirL[i], dirR[i] };

        for (int ch = 0; ch < numOut; ++ch)
        {
            // The dry path is delayed to match the splitter, so the two arrive
            // together and the host's latency compensation lines both up. The
            // live engine has no latency, and the dry path none either.
            float* d = buffer.getWritePointer (ch, startSample);
            float dry = d[i];
            if (dryLatency > 0)
            {
                dryDelay.pushSample (ch, dry);
                dry = dryDelay.popSample (ch);
            }

            const int side = juce::jmin (ch, 1);
            // The regions' own dry bands answer to their own levels only, not
            // to the global Dry (the untouched input) nor to Delays.
            d[i] = og * (dg * dry + dir[side] + wg * wet[side] + rg * rev[side]);
        }
    }
}

void DedeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    if (n <= 0 || numOut <= 0)
        return;

    // A mono input feeding a stereo bus: duplicate before anything else, so the
    // rest of the chain only ever sees a full set of channels.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, juce::jmax (0, juce::jmin (ch, numIn - 1)), 0, n);

    updateTransportAndMidi (midiMessages);

    updateBands();

    dryGain.setTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load()));
    wetGain.setTargetValue (juce::Decibels::decibelsToGain (pWetLevel->load()));
    revGain.setTargetValue (juce::Decibels::decibelsToGain (pRevLevel->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));

    // The freeze clock: the host's song position while it plays, running on
    // by itself from wherever it was while it does not. Chunks are cut at
    // every step boundary of a rhythmic freeze, so a step starts on its
    // sample (to the splitter's hop) whatever the block size.
    const double quartersPerSample = currentBpm.load() / (60.0 * sampleRate);
    if (hostPpq.has_value())
        freezeClockPpq = *hostPpq;

    const int maxChunk = juce::jmax (1, monoBuffer.getNumSamples());
    for (int start = 0; start < n;)
    {
        const int untilStep = runFreezeClock (freezeClockPpq + start * quartersPerSample,
                                              quartersPerSample);
        const int length = juce::jmin (maxChunk, n - start, untilStep);
        processChunk (buffer, start, length);
        start += length;
    }

    freezeClockPpq += n * quartersPerSample;

    // A mono output bus shows on both sides.
    for (int ch = 0; ch < 2; ++ch)
        outputMeters[ch].process (buffer.getReadPointer (juce::jmin (ch, numOut - 1)), n);
}

//==============================================================================
juce::AudioProcessorEditor* DedeAudioProcessor::createEditor()
{
    return new DedeAudioProcessorEditor (*this);
}

void DedeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("version", 1);
        copyXmlToBinary (*xml, destData);
    }
}

void DedeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // Future state versions are migrated here, by the "version" attribute.
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DedeAudioProcessor();
}
