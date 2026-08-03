/*
  ------------------------------------------------------------------------------
    PluginProcessor.h

    Dede: a spectral multi-delay and reverb.

    The signal is split into up to dede::maxRegions frequency regions drawn by
    the user on a spectrum view. Each region has a spectral gate, a gain and a
    pan, and feeds its own stereo delay (independent left and right times, left,
    right and cross feedback) plus a send to one shared reverb. The plugin
    output is the untouched dry signal plus the sum of the delays plus the
    reverb return.

    This file is the audio-thread owner of everything: the APVTS, the cached
    parameter pointers, the transport and MIDI state the delay times depend on,
    and the spectrum tap the editor draws.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "ParamIDs.h"

//==============================================================================
class DedeAudioProcessor : public juce::AudioProcessor
{
public:
    DedeAudioProcessor();
    ~DedeAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 8.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept  { return apvts; }
    fxme::PresetManager& getPresetManager() noexcept         { return presetManager; }

    /** The post-input spectrum the editor draws its regions over. */
    fxme::SpectrumTap& getInputTap() noexcept                { return inputTap; }

    /** Host tempo, or 120 when the host does not provide one. Read by the GUI
        so a sync-mode delay time can be shown in milliseconds. */
    double getCurrentBpm() const noexcept                    { return currentBpm.load(); }

    /** Frequency of the most recent MIDI note received, or 0 if none yet.
        MIDI-note mode multiplies its period by the region's delay value. */
    float getLastNoteHz() const noexcept                     { return lastNoteHz.load(); }

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Cached raw parameter pointers for one region slot. Looking parameters up
        by string in processBlock would allocate and hash per block; these are
        resolved once in the constructor and only read afterwards. */
    struct RegionParams
    {
        std::atomic<float>* on    = nullptr;
        std::atomic<float>* fLo   = nullptr;
        std::atomic<float>* fHi   = nullptr;
        std::atomic<float>* gate  = nullptr;
        std::atomic<float>* gain  = nullptr;
        std::atomic<float>* pan   = nullptr;
        std::atomic<float>* timeL = nullptr;
        std::atomic<float>* timeR = nullptr;
        std::atomic<float>* mode  = nullptr;
        std::atomic<float>* fbL   = nullptr;
        std::atomic<float>* fbR   = nullptr;
        std::atomic<float>* fbX   = nullptr;
        std::atomic<float>* send  = nullptr;
    };

    void cacheParameterPointers();

    /** Pulls tempo from the host and the last note number out of the MIDI
        buffer, so the delay-time modes have something to work from. */
    void updateTransportAndMidi (const juce::MidiBuffer& midi);

    juce::AudioProcessorValueTreeState apvts;
    fxme::PresetManager presetManager;

    RegionParams regions[dede::maxRegions];
    std::atomic<float>* pDryLevel = nullptr;
    std::atomic<float>* pWetLevel = nullptr;
    std::atomic<float>* pOutLevel = nullptr;
    std::atomic<float>* pRevRoom  = nullptr;
    std::atomic<float>* pRevDamp  = nullptr;
    std::atomic<float>* pRevWidth = nullptr;
    std::atomic<float>* pRevLevel = nullptr;

    /** Mono-sums a block into the spectrum tap in fixed-size chunks, so no
        scratch buffer has to be sized (or resized) on the audio thread. */
    void feedInputTap (const juce::AudioBuffer<float>& buffer);

    fxme::SpectrumTap inputTap;

    juce::SmoothedValue<float> dryGain, outGain;

    std::atomic<double> currentBpm { 120.0 };
    std::atomic<float>  lastNoteHz { 0.0f };

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedeAudioProcessor)
};
