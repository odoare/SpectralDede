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
#include <array>
#include <memory>
#include <optional>

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

    /** Region presets: what region slot `slot` loads and saves (everything
        but its frequency range, on, active and solo), as a PresetBank for
        the preset bar and browser. Its current preset is kept in the state. */
    fxme::ModulePresetTarget& getRegionPresets (int slot) noexcept { return *regionPresets[(size_t) slot]; }

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

    /** True while the bands are stereo (the `bands` parameter, once
        applied). Any thread. */
    bool areBandsStereo() const noexcept                     { return stereoBands.load(); }

    /** True while the live (filter bank) engine runs. Any thread. */
    bool isLiveEngine() const noexcept                       { return liveEngine.load(); }

    /** A region's band level in dB as its live gate compares it (see
        fxme::BandDynamicsProcessor), for the view's level markers; -200 in
        the spectral engine, whose gates compare the trace itself. Any
        thread. */
    float getBandLevelDb (int slot) const noexcept;

    /** A region's highest band level since the previous call (live engine),
        for the view's held maxima; -200 in the spectral engine. From one
        reader, the editor's timer. */
    float takeBandPeakLevelDb (int slot) noexcept;

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

        std::atomic<float>* gateRatio   = nullptr;
        std::atomic<float>* gateRange   = nullptr;
        std::atomic<float>* gateKnee    = nullptr;
        std::atomic<float>* gateAttack  = nullptr;
        std::atomic<float>* gateRelease = nullptr;
        std::atomic<float>* ceilRatio   = nullptr;
        std::atomic<float>* ceilKnee    = nullptr;
        std::atomic<float>* ceilAttack  = nullptr;
        std::atomic<float>* ceilRelease = nullptr;
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
        std::atomic<float>* transposeOn   = nullptr;
        std::atomic<float>* transposeSt   = nullptr;
        std::atomic<float>* satPos    = nullptr;
        std::atomic<float>* crushPos  = nullptr;
        std::atomic<float>* dsPos     = nullptr;
        std::atomic<float>* transposePos  = nullptr;
        std::atomic<float>* freqShiftOn  = nullptr;
        std::atomic<float>* freqShiftHz  = nullptr;
        std::atomic<float>* freqShiftPos = nullptr;

        std::atomic<float>* pitchOn    = nullptr;
        std::atomic<float>* pitchSt    = nullptr;
        std::atomic<float>* freezeOn   = nullptr;
        std::atomic<float>* freezeMode = nullptr;
        std::atomic<float>* freezeTrig = nullptr;
        std::atomic<float>* freezeRate = nullptr;
        std::atomic<float>* freezeWidth = nullptr;
        std::atomic<float>* blurOn     = nullptr;
        std::atomic<float>* blurMs     = nullptr;
    };

    void cacheParameterPointers();

    //==========================================================================
    // Engine and FFT size. Both parameters are read by prepareToPlay, and a
    // change while running is applied on the message thread (it
    // re-allocates and changes the latency).

    /** The FFT order the `fftSize` parameter asks for. */
    int requestedFftOrder() const;

    /** Whether the `engine` parameter asks for the live (filter bank) one. */
    bool requestedLiveEngine() const;

    /** Whether the `bands` parameter asks for stereo bands. */
    bool requestedStereoBands() const;

    /** Prepares the engine asked for (with configureSpectralEngine for the
        spectral one) and reports the latency; resets every region too when
        switching while running. Message thread, with processing suspended or
        not yet started. */
    void configureEngine (bool live, int order, bool stereo, bool resetRegions);

    /** Delays the dry path by `latency` samples and reports it to the host. */
    void setDryLatency (int latency);

    /** (Re)prepares everything sized by the FFT: the splitter, the spectral
        effects, the dry path's delay, and the reported latency. Message
        thread, with processing suspended or not yet started. */
    void configureSpectralEngine (int order);

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    std::atomic<int> activeFftOrder { dede::splitterFftOrder };
    std::atomic<bool> liveEngine { false };
    std::atomic<bool> stereoBands { true };
    int dryLatency = 0;
    int preparedBlockSize = 0;

    /** Pulls tempo from the host and the last note number out of the MIDI
        buffer, so the delay-time modes have something to work from. */
    void updateTransportAndMidi (const juce::MidiBuffer& midi);

    juce::AudioProcessorValueTreeState apvts;
    fxme::PresetManager presetManager;

    /** Region presets: one library for every slot (so a preset saved from
        one region is in every region's list), one target per slot. The
        targets are made in the constructor, once the parameters exist, and
        go before the library and the APVTS (declared after them). */
    fxme::ModulePresetLibrary regionPresetLibrary;
    std::array<std::unique_ptr<fxme::ModulePresetTarget>, dede::maxRegions> regionPresets;

    RegionParams regions[dede::maxRegions];
    std::atomic<float>* pDryLevel = nullptr;
    std::atomic<float>* pWetLevel = nullptr;
    std::atomic<float>* pOutLevel = nullptr;
    std::atomic<float>* pRevRoom  = nullptr;
    std::atomic<float>* pRevDamp  = nullptr;
    std::atomic<float>* pRevWidth = nullptr;
    std::atomic<float>* pRevLevel = nullptr;
    std::atomic<float>* pFftSize  = nullptr;
    std::atomic<float>* pEngine   = nullptr;
    std::atomic<float>* pBandMode = nullptr;
    std::atomic<float>* pPanLaw   = nullptr;

    /** Mono-sums the input into monoBuffer: what the spectrum view shows and
        what the band splitter is fed are then one and the same signal, which
        is what makes a gate line drawn on the trace mean anything. */
    void buildMonoInput (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    /** One region's dynamics (the Dynamics tab) as the splitters take them. */
    fxme::BandDynamics regionDynamics (int slot) const;
    static bool sameDynamics (const fxme::BandDynamics& a, const fxme::BandDynamics& b) noexcept;

    /** What was last passed to the active splitter, per region, and whether
        it has to be sent again (set by configureEngine(), which every prepare
        goes through: a freshly prepared splitter has default dynamics). */
    fxme::BandDynamics sentDynamics[dede::maxRegions];
    bool dynamicsStale[dede::maxRegions] {};

    /** Copies the current region parameters into the splitter's bands. */
    void updateBands();

    /** A region's freeze trigger (manual, retrigger, on/off). */
    dede::FreezeTrigger freezeTrigger (int slot) const noexcept;

    /** Applies the rhythmic freezes at song position `ppq` (in quarter
        notes): gates the on/off ones, retriggers the others on a new step.
        Returns how many samples until the next step boundary of any of them
        (int max when none is rhythmic). Audio thread. */
    int runFreezeClock (double ppq, double quartersPerSample);

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

    /** Indices into a region's pan matrix: left into left, right into left,
        left into right, right into right. */
    enum { panLL = 0, panRL, panLR, panRR };

    /** One region's pan as a 2 x 2 matrix: constant power for a mono band,
        balance or stereo pan (the `panlaw` parameter) for a stereo one. */
    void panMatrix (int slot, float (&m)[4]) const;

    /** Snaps a region's pan smoothers to its matrix, no glide. */
    void snapPan (int slot);

    /** Reads one region's degrade parameters for the block: which stages
        are on and where, their settings, and resets a stage that has just
        come on or moved (it starts from rest in its new place). */
    void beginDegradeBlock (int slot);

    /** One frame (1 channel before the delay, 2 in the loop and after it)
        through the region's stages that sit at `position`, in their fixed
        order: saturation, bit-crush, downsample, transpose, freq shift. */
    void degradeFrame (int slot, dede::DegradePosition position, float* frame, int numChannels) noexcept;

    /** Clears one region's degrade state (every stage starts from rest). */
    void resetDegrade (int slot);

    /** Shortest delay line time once a loop Transpose's latency is taken off. */
    static constexpr float minLoopSeconds = 0.001f;

    /** Whether any of the region's stages is on at `position` this block. */
    bool hasDegradeAt (int slot, dede::DegradePosition position) const noexcept;

    /** True when any region in use is soloed. */
    bool anySolo() const noexcept;

    /** Whether a region is heard: in use, and soloed if anything
        is. Drives its output gain (ramped). */
    bool isAudible (int slot, bool soloing) const noexcept;

    /** Clears every piece of per-region state, for a slot being used again:
        its delay lines, degrade chain, spectral effects
        and meters, and snaps its smoothed levels to the new parameters. */
    void resetRegion (int slot);

    fxme::SpectrumTap inputTap;
    fxme::SpectralBandSplitter splitter;

    /** Pitch, freeze and blur, run by the splitter inside its frames (it is
        installed as the splitter's band processor in the constructor). */
    fxme::SpectralBandEffects spectral;

    /** The live engine: the same bands cut by filters, no latency. Only the
        active one of the two splitters is prepared, fed and read. */
    fxme::FilterBankSplitter filterBank;
    fxme::StereoCrossDelay delays[dede::maxRegions];

    /** One region's degrade chain. Before the delay the band is mono (the
        pan comes after) and only channel 0 is used; in the loop and after
        the delay both are. A stage sits at one position at a time. */
    struct Degrade
    {
        fxme::Saturator    sat[2];
        fxme::LevelMatcher satLevel[2];     // the saturator's auto-gain
        fxme::BitCrusher   crush[2];
        fxme::Downsampler  ds[2];
        fxme::SplicePitchShifter transpose;
        fxme::FrequencyShifter freqShift;   // stereo, one oscillator     // stereo, one set of splices
        juce::SmoothedValue<float> satDrive;

        /** Where each stage runs this block, -1 when off. */
        int satPos = -1, crushPos = -1, dsPos = -1, transposePos = -1, freqShiftPos = -1;
    };
    Degrade degrade[dede::maxRegions];

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
    juce::SmoothedValue<float> pan[dede::maxRegions][4];   // see panMatrix()

    std::atomic<double> currentBpm { 120.0 };

    /** The host's song position at the block's start, while it plays. */
    std::optional<double> hostPpq;

    /** The freeze clock, in quarter notes (see processBlock), each region's
        last step on it, and the spectral settings last sent (the clock
        gates their freeze). Audio thread. */
    double freezeClockPpq = 0.0;
    static constexpr int64_t noFreezeStep = std::numeric_limits<int64_t>::min();
    int64_t freezeStep[dede::maxRegions] {};
    fxme::SpectralBandEffects::Settings bandEffects[dede::maxRegions];
    std::atomic<float>  lastNoteHz { 0.0f };

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedeAudioProcessor)
};
