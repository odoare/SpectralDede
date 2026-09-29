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
#include <FxmeTools/dsp/Reverb.h>   // not in the module umbrella: needs WDL
#include "ParamIDs.h"

//==============================================================================
class DedeAudioProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
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

    /** The FFT order the splitter is running at (the `fftSize` parameter once
        applied). The spectrum view follows it; any thread. */
    int getFftOrder() const noexcept                         { return activeFftOrder.load(); }

    /** A region's output level in dBFS (RMS over 100 ms), channel 0 or 1,
        measured before the solo gain. For the region column's meters; any
        thread. */
    float getRegionLevelDb (int slot, int channel) const noexcept;

    /** The plugin's output level in dBFS, channel 0 or 1, RMS over 100 ms.
        For the output meter; any thread. */
    float getOutputLevelDb (int channel) const noexcept;

    /** The output's highest peak in dBFS since the last call, channel 0 or 1.
        For the output meter, from one reader (the GUI timer). */
    float takeOutputPeakDb (int channel) noexcept;

    /** How much of a region's energy its gate and ceiling let through, 0 to 1
        (see fxme::SpectralBandSplitter::getGateOpenness). Any thread. */
    float getGateOpenness (int slot) const noexcept;

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Cached raw parameter pointers for one region slot. Looking parameters up
        by string in processBlock would allocate and hash per block; these are
        resolved once in the constructor and only read afterwards. */
    struct RegionParams
    {
        std::atomic<float>* on    = nullptr;
        std::atomic<float>* active = nullptr;
        std::atomic<float>* solo  = nullptr;
        std::atomic<float>* fLo   = nullptr;
        std::atomic<float>* fHi   = nullptr;
        std::atomic<float>* gate  = nullptr;
        std::atomic<float>* ceil  = nullptr;
        std::atomic<float>* gain  = nullptr;
        std::atomic<float>* pan   = nullptr;
        std::atomic<float>* timeL = nullptr;
        std::atomic<float>* timeR = nullptr;
        std::atomic<float>* mode  = nullptr;
        std::atomic<float>* fbL   = nullptr;
        std::atomic<float>* fbR   = nullptr;
        std::atomic<float>* fbX   = nullptr;
        std::atomic<float>* dry   = nullptr;
        std::atomic<float>* dly   = nullptr;
        std::atomic<float>* send  = nullptr;

        std::atomic<float>* satOn     = nullptr;
        std::atomic<float>* satModel  = nullptr;
        std::atomic<float>* satDrive  = nullptr;
        std::atomic<float>* crushOn   = nullptr;
        std::atomic<float>* crushBits = nullptr;
        std::atomic<float>* dsOn      = nullptr;
        std::atomic<float>* dsFactor  = nullptr;
        std::atomic<float>* keep      = nullptr;

        std::atomic<float>* pitchOn    = nullptr;
        std::atomic<float>* pitchSt    = nullptr;
        std::atomic<float>* freezeOn   = nullptr;
        std::atomic<float>* freezeMode = nullptr;
        std::atomic<float>* blurOn     = nullptr;
        std::atomic<float>* blurMs     = nullptr;
    };

    void cacheParameterPointers();

    //==========================================================================
    // FFT size. The parameter is read by prepareToPlay, and a change while
    // running is applied on the message thread (it re-allocates).

    /** The FFT order the `fftSize` parameter asks for. */
    int requestedFftOrder() const;

    /** (Re)prepares everything sized by the FFT: the splitter, the spectral
        effects, the dry path's delay, and the reported latency. Message
        thread, with processing suspended or not yet started. */
    void configureSpectralEngine (int order);

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    std::atomic<int> activeFftOrder { dede::splitterFftOrder };
    int preparedBlockSize = 0;

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
    std::atomic<float>* pGateKnee = nullptr;
    std::atomic<float>* pGateAttack  = nullptr;
    std::atomic<float>* pGateRelease = nullptr;
    std::atomic<float>* pFftSize  = nullptr;

    /** Mono-sums the input into monoBuffer: what the spectrum view shows and
        what the band splitter is fed are then one and the same signal, which
        is what makes a gate line drawn on the trace mean anything. */
    void buildMonoInput (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    /** Copies the current region parameters into the splitter's bands. */
    void updateBands();

    /** The whole wet path for at most one prepared block's worth of samples.
        processBlock chunks by this so a host that hands over more than the
        block size it promised cannot run past the scratch buffers. */
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    /** Reads one region's delay-time parameters through the resolver, which
        needs the transport and the last MIDI note to make sense of the sync
        and note modes. */
    void updateDelay (int slot);

    /** One region's send to the reverb as a linear gain, with the bottom of
        the range read as silence. */
    float sendLevel (int slot) const;

    /** One region's delay output into the mix as a linear gain, with the
        bottom of the range read as silence. */
    float delayLevel (int slot) const;

    /** One region's own band (degraded, panned, not delayed) into the mix as
        a linear gain, with the bottom of the range read as silence. */
    float regionDryLevel (int slot) const;

    /** One region's constant-power pan as a pair of gains. */
    void panGains (int slot, float& left, float& right) const;

    /** Runs one region's mono band through its degrade chain in place:
        saturation, bit-crush, downsample, each skipped when off. */
    void applyDegrade (int slot, float* x, int numSamples);

    /** Band-passes one region's mono band back to its range (shifted by its
        pitch, if on) when its "keep in band" switch is on; nothing when off. */
    void applyKeepInBand (int slot, float* x, int numSamples);

    /** True when any region in use is soloed. */
    bool anySolo() const noexcept;

    /** Whether a region is heard: in use, and soloed if anything
        is. Drives its output gain (ramped). */
    bool isAudible (int slot, bool soloing) const noexcept;

    /** Clears every piece of per-region state, for a slot being used again:
        its delay lines, degrade and keep-in-band filters, spectral effects
        and meters, and snaps its smoothed levels to the new parameters. */
    void resetRegion (int slot);

    fxme::SpectrumTap inputTap;
    fxme::SpectralBandSplitter splitter;

    /** Pitch, freeze and blur, run by the splitter inside its frames (it is
        installed as the splitter's band processor in the constructor). */
    fxme::SpectralBandEffects spectral;
    fxme::StereoCrossDelay delays[dede::maxRegions];

    // Degrade chain, one set per region. The band is mono at that point (the
    // pan comes after), so one instance of each per region is enough.
    fxme::Saturator   saturators[dede::maxRegions];
    fxme::BitCrusher  crushers[dede::maxRegions];
    fxme::Downsampler downsamplers[dede::maxRegions];
    juce::SmoothedValue<float> satDrive[dede::maxRegions];
    fxme::LevelMatcher satLevel[dede::maxRegions];   // the saturator's auto-gain
    bool satWasOn[dede::maxRegions] {};
    bool dsWasOn[dede::maxRegions] {};

    /** "Keep in band": 24 dB/octave on each side, after the degrade chain. */
    fxme::EdgeBandPass keepFilters[dede::maxRegions];
    bool keepWasOn[dede::maxRegions] {};

    /** Solo and a freed slot's fade-out: the region's output gain. */
    juce::SmoothedValue<float> audibleGain[dede::maxRegions];

    /** Set once a freed slot has faded out: its state is cleared when the
        slot is used again. */
    bool needsReset[dede::maxRegions] {};

    /** Each region's output level (L, R), before the solo gain, for the
        region column. */
    fxme::VuMeter meters[dede::maxRegions][2];

    /** The plugin's output (L, R), after the output trim. */
    fxme::VuMeter outputMeters[2];
    fxme::Reverb reverb;

    /** The dry path is delayed by the splitter's latency so it stays lined up
        with the regions; the total is then reported to the host, which shifts
        the whole thing back into place. */
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };

    juce::AudioBuffer<float> monoBuffer;    // 1 channel, the splitter's input
    juce::AudioBuffer<float> regionBuffer;  // stereo, one region's delay output
    juce::AudioBuffer<float> wetBuffer;     // stereo, the summed regions
    juce::AudioBuffer<float> sendBuffer;    // stereo, what the regions send to the reverb
    juce::AudioBuffer<float> directBuffer;  // stereo, the regions' own dry bands, summed
    juce::AudioBuffer<float> meterBuffer;   // stereo, one region's output before the solo gain, for its meter
    juce::AudioBuffer<float> bandBuffer;    // 1 channel, one region's band through its degrade chain
    juce::AudioBuffer<float> panBuffer;     // stereo, that band panned, into its delay

    juce::SmoothedValue<float> dryGain, wetGain, revGain, outGain;

    /** One smoother per region so a send can be moved while a delay is
        ringing without stepping the tail. */
    juce::SmoothedValue<float> sendGain[dede::maxRegions];
    juce::SmoothedValue<float> delayGain[dede::maxRegions];
    juce::SmoothedValue<float> regionDryGain[dede::maxRegions];

    /** The pan, applied after the degrade chain (the splitter's own pan is
        off), smoothed like the splitter smoothed it. */
    juce::SmoothedValue<float> panL[dede::maxRegions], panR[dede::maxRegions];

    std::atomic<double> currentBpm { 120.0 };
    std::atomic<float>  lastNoteHz { 0.0f };

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedeAudioProcessor)
};
