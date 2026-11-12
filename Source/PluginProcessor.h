#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/BaxEngine.h"

//==============================================================================
class BaxAudioProcessor : public juce::AudioProcessor
{
public:
    BaxAudioProcessor();
    ~BaxAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override          { return JucePlugin_Name; }
    bool acceptsMidi() const override                    { return false; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 0.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override                     { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Settings as the DSP sees them (view / stereo link resolved). Message or audio thread. */
    bax::EngineSettings readSettings() const;

    bax::View getView() const;
    bool isLinkOn() const;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    /** Sample rate for the editor's response display. */
    std::atomic<double> displaySampleRate { 48000.0 };
    std::atomic<float> editorScale { 1.0f };   // remembered editor size (1 = 100 %)

private:
    template <typename Sample>
    void processInternal (juce::AudioBuffer<Sample>&);
    template <typename Sample>
    void bypassInternal (juce::AudioBuffer<Sample>&);
    void setParameterValue (const char* id, float plainValue);

    static constexpr int kChunk = 256;
    bax::BaxEngine engine;
    double scratchL[kChunk] {}, scratchR[kChunk] {};

    struct ChannelPointers { std::atomic<float> *hp, *lsFreq, *lsGain, *hsGain, *hsFreq, *lp, *engage; };
    ChannelPointers ch[2] {};
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pMidSide = nullptr;
    std::atomic<float>* pLink = nullptr;
    std::atomic<float>* pOversampling = nullptr;
    std::atomic<float>* pView = nullptr;
    std::atomic<float>* pMonitor = nullptr;
    std::atomic<float>* pBypass = nullptr;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BaxAudioProcessor)
};
