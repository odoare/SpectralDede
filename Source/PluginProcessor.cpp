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

    // ---- Spectral gate, shared by every region ------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::gateKnee, 1 }, "Gate knee",
        NormalisableRange<float> (0.0f, maxGateKneeDb, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float x, int) { return x <= 0.0f ? String ("hard")
                                                 : String (x, 1) + " dB"; })));

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
    pGateKnee = raw (ids::gateKnee);

    for (int i = 0; i < maxRegions; ++i)
    {
        auto& r = regions[i];
        r.on    = raw (ids::region (i, ids::rgOn));
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
        r.send  = raw (ids::region (i, ids::rgSend));
    }
}

//==============================================================================
void DedeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;

    splitter.prepare (sampleRate, samplesPerBlock, maxRegions, splitterFftOrder);

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
    monoBuffer.clear();
    regionBuffer.clear();
    wetBuffer.clear();
    sendBuffer.clear();

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    dryDelay.setMaximumDelayInSamples (juce::jmax (1, splitter.getLatencySamples()));
    dryDelay.prepare (spec);
    dryDelay.setDelay ((float) splitter.getLatencySamples());
    dryDelay.reset();

    setLatencySamples (splitter.getLatencySamples());

    dryGain.reset (sampleRate, 0.02);
    wetGain.reset (sampleRate, 0.02);
    revGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    dryGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load()));
    wetGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pWetLevel->load()));
    revGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pRevLevel->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));

    for (int i = 0; i < maxRegions; ++i)
    {
        sendGain[i].reset (sampleRate, 0.02);
        sendGain[i].setCurrentAndTargetValue (sendLevel (i));
    }
}

void DedeAudioProcessor::releaseResources()
{
    splitter.reset();
    dryDelay.reset();
    reverb.reset();
    for (auto& d : delays)
        d.reset();
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
    for (int i = 0; i < maxRegions; ++i)
    {
        const auto& r = regions[i];

        // A region whose borders have been dragged past each other is still a
        // valid region: take the interval either way round.
        const float a = r.fLo->load(), b = r.fHi->load();
        const float gateDb = r.gate->load();
        const float ceilDb = r.ceil->load();

        fxme::SpectralBand band;
        band.enabled = r.on->load() > 0.5f;
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

        splitter.setBand (i, band);
    }
}

float DedeAudioProcessor::sendLevel (int slot) const
{
    const float db = regions[slot].send->load();
    return db <= minFeedbackDb ? 0.0f : juce::Decibels::decibelsToGain (db);
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

    delays[slot].setDelaySeconds (resolver.toSeconds (r.timeL->load(), mode),
                                  resolver.toSeconds (r.timeR->load(), mode));

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

    // The band splitter and the spectrum view both work on the mono sum, so
    // the gate thresholds the user draws on the trace are the ones the DSP
    // compares against.
    buildMonoInput (buffer, startSample, numSamples);
    inputTap.push (monoBuffer.getReadPointer (0), numSamples);

    splitter.process (monoBuffer.getReadPointer (0), numSamples);

    // Each region's band goes through its own stereo delay, and the delay
    // outputs are what the wet bus carries: the region's direct sound is
    // already there in the untouched dry path.
    wetBuffer.clear (0, numSamples);
    sendBuffer.clear (0, numSamples);

    for (int i = 0; i < maxRegions; ++i)
    {
        if (regions[i].on->load() <= 0.5f)
            continue;

        updateDelay (i);
        delays[i].process (splitter.getBandOutput (i, 0), splitter.getBandOutput (i, 1),
                           regionBuffer.getWritePointer (0), regionBuffer.getWritePointer (1),
                           numSamples);

        // The send is taken after the delay, so what reaches the reverb is the
        // region's repeats rather than its dry band.
        sendGain[i].setTargetValue (sendLevel (i));

        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::add (wetBuffer.getWritePointer (ch),
                                              regionBuffer.getReadPointer (ch), numSamples);

        auto* sendL = sendBuffer.getWritePointer (0);
        auto* sendR = sendBuffer.getWritePointer (1);
        const auto* regL = regionBuffer.getReadPointer (0);
        const auto* regR = regionBuffer.getReadPointer (1);

        for (int s = 0; s < numSamples; ++s)
        {
            const float g = sendGain[i].getNextValue();
            sendL[s] += regL[s] * g;
            sendR[s] += regR[s] * g;
        }
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

    for (int i = 0; i < numSamples; ++i)
    {
        const float dg = dryGain.getNextValue();
        const float wg = wetGain.getNextValue();
        const float rg = revGain.getNextValue();
        const float og = outGain.getNextValue();
        const float wet[2] { wetL[i], wetR[i] };
        const float rev[2] { revL[i], revR[i] };

        for (int ch = 0; ch < numOut; ++ch)
        {
            // The dry path is delayed to match the splitter, so the two arrive
            // together and the host's latency compensation lines both up.
            float* d = buffer.getWritePointer (ch, startSample);
            dryDelay.pushSample (ch, d[i]);
            const float dry = dryDelay.popSample (ch);

            const int side = juce::jmin (ch, 1);
            d[i] = og * (dg * dry + wg * wet[side] + rg * rev[side]);
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

    // Before updateBands(): a knee change recomputes every band's thresholds,
    // and setBand() then works from the new knee.
    splitter.setGateKnee (pGateKnee->load());
    updateBands();

    dryGain.setTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load()));
    wetGain.setTargetValue (juce::Decibels::decibelsToGain (pWetLevel->load()));
    revGain.setTargetValue (juce::Decibels::decibelsToGain (pRevLevel->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));

    const int maxChunk = juce::jmax (1, monoBuffer.getNumSamples());
    for (int start = 0; start < n; start += maxChunk)
        processChunk (buffer, start, juce::jmin (maxChunk, n - start));
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
