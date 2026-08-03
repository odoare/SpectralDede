/*
  ------------------------------------------------------------------------------
    PluginProcessor.cpp

    See PluginProcessor.h for what Dede is.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
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

    // ---- Region slots ------------------------------------------------------
    const StringArray modeNames { "Seconds", "DAW sync", "MIDI note" };

    for (int i = 0; i < maxRegions; ++i)
    {
        const String slot = "R" + String (i + 1) + " ";
        const auto id = [i] (const char* suffix) { return ids::region (i, suffix); };

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id (ids::rgOn), 1 }, slot + "On", false));

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
            ParameterID { id (ids::rgGain), 1 }, slot + "Gain",
            NormalisableRange<float> (minGainDb, maxGainDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgPan), 1 }, slot + "Pan",
            NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgTimeL), 1 }, slot + "Time L",
            logRange (minDelayValue, maxDelayValue), 0.25f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgTimeR), 1 }, slot + "Time R",
            logRange (minDelayValue, maxDelayValue), 0.375f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id (ids::rgMode), 1 }, slot + "Mode",
            modeNames, (int) DelayMode::seconds));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFbL), 1 }, slot + "Fb L",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), -12.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFbR), 1 }, slot + "Fb R",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), -12.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgFbX), 1 }, slot + "Cross Fb",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), minFeedbackDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgSend), 1 }, slot + "Reverb Send",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), minFeedbackDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));
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
                     fxme::PresetManager::getDefaultUserPresetDirectory ("Dede")
                   #if DEDE_HAS_BINARY_DATA
                     , BinaryData::namedResourceList,
                     BinaryData::namedResourceListSize,
                     BinaryData::getNamedResource
                   #endif
                     )
{
    cacheParameterPointers();
    inputTap.setEnabled (true);
}

DedeAudioProcessor::~DedeAudioProcessor() = default;

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

    for (int i = 0; i < maxRegions; ++i)
    {
        auto& r = regions[i];
        r.on    = raw (ids::region (i, ids::rgOn));
        r.fLo   = raw (ids::region (i, ids::rgFLo));
        r.fHi   = raw (ids::region (i, ids::rgFHi));
        r.gate  = raw (ids::region (i, ids::rgGate));
        r.gain  = raw (ids::region (i, ids::rgGain));
        r.pan   = raw (ids::region (i, ids::rgPan));
        r.timeL = raw (ids::region (i, ids::rgTimeL));
        r.timeR = raw (ids::region (i, ids::rgTimeR));
        r.mode  = raw (ids::region (i, ids::rgMode));
        r.fbL   = raw (ids::region (i, ids::rgFbL));
        r.fbR   = raw (ids::region (i, ids::rgFbR));
        r.fbX   = raw (ids::region (i, ids::rgFbX));
        r.send  = raw (ids::region (i, ids::rgSend));
    }
}

//==============================================================================
void DedeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = newSampleRate;

    dryGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    dryGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));
}

void DedeAudioProcessor::releaseResources()
{
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
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                currentBpm.store (*bpm > 0.0 ? *bpm : 120.0);

    // The *last* note of the block wins: MIDI-note mode tracks whatever was
    // played most recently, and re-triggering the same note is a no-op.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            lastNoteHz.store ((float) m.getMidiNoteInHertz (m.getNoteNumber()));
    }
}

void DedeAudioProcessor::feedInputTap (const juce::AudioBuffer<float>& buffer)
{
    if (! inputTap.isEnabled())
        return;

    constexpr int chunk = 256;
    float mono[chunk];

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();
    const float norm = numCh > 0 ? 1.0f / (float) numCh : 1.0f;

    for (int start = 0; start < n; start += chunk)
    {
        const int count = juce::jmin (chunk, n - start);

        for (int i = 0; i < count; ++i)
            mono[i] = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = buffer.getReadPointer (ch, start);
            for (int i = 0; i < count; ++i)
                mono[i] += src[i] * norm;
        }

        inputTap.push (mono, count);
    }
}

void DedeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    // A mono input feeding a stereo bus: duplicate before anything else, so the
    // rest of the chain only ever sees a full set of channels.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, juce::jmin (ch, numIn - 1), 0, n);

    updateTransportAndMidi (midiMessages);
    feedInputTap (buffer);

    // Step 1 is the dry path only: the spectral band splitter, the per-region
    // delays and the reverb are added in the following steps.
    dryGain.setTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));

    for (int i = 0; i < n; ++i)
    {
        const float g = dryGain.getNextValue() * outGain.getNextValue();
        for (int ch = 0; ch < numOut; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
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
