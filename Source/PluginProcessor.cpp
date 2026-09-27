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

        // The region's own band, degraded and panned but not delayed, into
        // the mix. Off by default: the band is already in the global dry, and
        // this adds to it rather than replacing it.
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgDry), 1 }, slot + "Dry Level",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), minFeedbackDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        // The region's repeats into the mix.
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgDly), 1 }, slot + "Delay Level",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id (ids::rgSend), 1 }, slot + "Reverb Send",
            NormalisableRange<float> (minFeedbackDb, maxFeedbackDb, 0.1f), minFeedbackDb,
            AudioParameterFloatAttributes().withStringFromValueFunction (dbToText)));

        // ---- Degrade: saturation -> bit-crush -> downsample, all off ----------
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
    }
}

//==============================================================================
void DedeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;

    splitter.prepare (sampleRate, samplesPerBlock, maxRegions, splitterFftOrder);

    // Each band leaves the splitter mono (gain applied, no pan) so the degrade
    // chain treats both sides alike; the pan is applied after it, here.
    splitter.setApplyPan (false);

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
    directBuffer.setSize (2, samplesPerBlock, false, false, true);
    directBuffer.clear();
    bandBuffer.setSize (1, samplesPerBlock, false, false, true);
    panBuffer.setSize (2, samplesPerBlock, false, false, true);
    bandBuffer.clear();
    panBuffer.clear();
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
        delayGain[i].reset (sampleRate, 0.02);
        delayGain[i].setCurrentAndTargetValue (delayLevel (i));
        regionDryGain[i].reset (sampleRate, 0.02);
        regionDryGain[i].setCurrentAndTargetValue (regionDryLevel (i));

        float left = 0.0f, right = 0.0f;
        panGains (i, left, right);
        panL[i].reset (sampleRate, 0.02);
        panR[i].reset (sampleRate, 0.02);
        panL[i].setCurrentAndTargetValue (left);
        panR[i].setCurrentAndTargetValue (right);

        saturators[i].prepare (sampleRate);
        satDrive[i].reset (sampleRate, 0.02);
        downsamplers[i].reset();
        satWasOn[i] = dsWasOn[i] = false;
    }
}

void DedeAudioProcessor::releaseResources()
{
    splitter.reset();
    dryDelay.reset();
    reverb.reset();
    for (auto& d : delays)
        d.reset();
    for (auto& s : saturators)
        s.reset();
    for (auto& d : downsamplers)
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

void DedeAudioProcessor::panGains (int slot, float& left, float& right) const
{
    // Constant power, as the splitter did before the pan moved here: sweeping
    // a region across the image keeps its loudness.
    const float theta = (juce::jlimit (-1.0f, 1.0f, regions[slot].pan->load()) + 1.0f)
                            * juce::MathConstants<float>::pi * 0.25f;
    left  = std::cos (theta);
    right = std::sin (theta);
}

void DedeAudioProcessor::applyDegrade (int slot, float* x, int numSamples)
{
    const auto& r = regions[slot];

    // Stages that are off cost nothing: they are skipped, not run neutral.
    const bool satOn = r.satOn->load() > 0.5f;
    if (satOn)
    {
        auto& sat = saturators[slot];
        const float driveGain = juce::Decibels::decibelsToGain (r.satDrive->load());

        // Coming back on: forget the DC blocker and sag state of the last
        // time, and start the drive where it is rather than gliding to it.
        if (! satWasOn[slot])
        {
            sat.reset();
            satDrive[slot].setCurrentAndTargetValue (driveGain);
        }

        // Bias and sag are not parameters; fixed values give each model its
        // character. Sag makes Dynamic differ from Standard at all, and a
        // negative overlap is what gives Class AB its crossover notch.
        using Model = fxme::Saturator::Model;
        const auto model = (Model) juce::jlimit (0, 3, (int) r.satModel->load());
        sat.setModel (model);
        sat.setSag (model == Model::Standard ? 0.0f : 0.5f);
        sat.setBias (model == Model::ClassAB ? -0.1f : 0.0f);

        satDrive[slot].setTargetValue (driveGain);

        // Divided back by the drive: unity for small signals, so the drive
        // sets how hard the band clips rather than how loud it comes out.
        for (int s = 0; s < numSamples; ++s)
        {
            const float g = satDrive[slot].getNextValue();
            sat.setDriveGain (g);
            x[s] = sat.processSample (x[s]) / g;
        }
    }
    satWasOn[slot] = satOn;

    if (r.crushOn->load() > 0.5f)
    {
        auto& crusher = crushers[slot];
        crusher.setBits (r.crushBits->load());
        for (int s = 0; s < numSamples; ++s)
            x[s] = crusher.processSample (x[s]);
    }

    const bool dsOn = r.dsOn->load() > 0.5f;
    if (dsOn)
    {
        auto& ds = downsamplers[slot];
        if (! dsWasOn[slot])
            ds.reset();

        ds.setFactor (r.dsFactor->load());
        for (int s = 0; s < numSamples; ++s)
            x[s] = ds.processSample (x[s]);
    }
    dsWasOn[slot] = dsOn;
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
    // outputs are what the wet bus carries. The region's own band, undelayed,
    // is already in the untouched dry path; its Dry level adds the processed
    // version on top, on a bus of its own (directBuffer).
    wetBuffer.clear (0, numSamples);
    sendBuffer.clear (0, numSamples);
    directBuffer.clear (0, numSamples);

    for (int i = 0; i < maxRegions; ++i)
    {
        if (regions[i].on->load() <= 0.5f)
            continue;

        updateDelay (i);

        // Band (mono, gain applied) -> degrade -> pan -> the region's delay.
        float* band = bandBuffer.getWritePointer (0);
        juce::FloatVectorOperations::copy (band, splitter.getBandOutput (i, 0), numSamples);
        applyDegrade (i, band, numSamples);

        float left = 0.0f, right = 0.0f;
        panGains (i, left, right);
        panL[i].setTargetValue (left);
        panR[i].setTargetValue (right);

        float* pl = panBuffer.getWritePointer (0);
        float* pr = panBuffer.getWritePointer (1);
        for (int s = 0; s < numSamples; ++s)
        {
            pl[s] = band[s] * panL[i].getNextValue();
            pr[s] = band[s] * panR[i].getNextValue();
        }

        delays[i].process (pl, pr,
                           regionBuffer.getWritePointer (0), regionBuffer.getWritePointer (1),
                           numSamples);

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

            // pl/pr still hold the panned band: the delay wrote elsewhere.
            const float directL  = pl[s] * dry,     directR  = pr[s] * dry;
            const float delayedL = regL[s] * delay, delayedR = regR[s] * delay;

            dirL[s]  += directL;
            dirR[s]  += directR;
            wetL[s]  += delayedL;
            wetR[s]  += delayedR;
            sendL[s] += (directL + delayedL) * send;
            sendR[s] += (directR + delayedR) * send;
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
            // together and the host's latency compensation lines both up.
            float* d = buffer.getWritePointer (ch, startSample);
            dryDelay.pushSample (ch, d[i]);
            const float dry = dryDelay.popSample (ch);

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
