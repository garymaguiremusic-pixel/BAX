#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace bax;

//==============================================================================
BaxAudioProcessor::BaxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BAXR", createParameterLayout())
{
    for (int c = 0; c < 2; ++c)
    {
        const auto& ids = kChannelIDs[c];
        ch[c] = { apvts.getRawParameterValue (ids.hp),     apvts.getRawParameterValue (ids.lsFreq),
                  apvts.getRawParameterValue (ids.lsGain), apvts.getRawParameterValue (ids.hsGain),
                  apvts.getRawParameterValue (ids.hsFreq), apvts.getRawParameterValue (ids.lp),
                  apvts.getRawParameterValue (ids.engage) };
    }
    pOutput       = apvts.getRawParameterValue (ParamIDs::output);
    pMidSide      = apvts.getRawParameterValue (ParamIDs::midSide);
    pLink         = apvts.getRawParameterValue (ParamIDs::link);
    pOversampling = apvts.getRawParameterValue (ParamIDs::oversampling);
    pView         = apvts.getRawParameterValue (ParamIDs::view);
    pMonitor      = apvts.getRawParameterValue (ParamIDs::monitor);
    pBypass       = apvts.getRawParameterValue (ParamIDs::bypass);
    bypassParam   = apvts.getParameter (ParamIDs::bypass);

    // One latency for every rate and oversampling factor, so it never changes.
    setLatencySamples (BaxEngine::kLatency);
}

//==============================================================================
View BaxAudioProcessor::getView() const
{
    return (View) juce::jlimit (0, 2, (int) pView->load());
}

bool BaxAudioProcessor::isLinkOn() const
{
    return pLink->load() > 0.5f;
}

EngineSettings BaxAudioProcessor::readSettings() const
{
    auto readChannel = [] (const ChannelPointers& p)
    {
        ChannelSettings s;
        s.hp       = detentFromNormalised (p.hp->load());
        s.lsFreq   = detentFromNormalised (p.lsFreq->load());
        s.hsFreq   = detentFromNormalised (p.hsFreq->load());
        s.lp       = detentFromNormalised (p.lp->load());
        s.lsGainDb = p.lsGain->load();
        s.hsGainDb = p.hsGain->load();
        return s;
    };

    // Classic / Classic Exp: one set of frequency selectors and one Engage switch (the
    // left ones) drive both channels; the low / high levels stay per channel unless
    // Stereo link is on. Dual Mono: everything per channel; Stereo link copies the
    // whole left channel to the right.
    EngineSettings s;
    const bool link = isLinkOn();
    s.a = readChannel (ch[0]);
    s.b = readChannel (ch[1]);
    s.engagedA = ch[0].engage->load() < 0.5f;                       // choice 0 = On
    s.engagedB = ch[1].engage->load() < 0.5f;
    if (getView() != View::DualMono)
    {
        s.b.hp = s.a.hp; s.b.lsFreq = s.a.lsFreq; s.b.hsFreq = s.a.hsFreq; s.b.lp = s.a.lp;
        s.engagedB = s.engagedA;
        if (link) { s.b.lsGainDb = s.a.lsGainDb; s.b.hsGainDb = s.a.hsGainDb; }
    }
    else if (link)
    {
        s.b = s.a;
        s.engagedB = s.engagedA;
    }
    s.midSide = pMidSide->load() > 0.5f;
    s.oversampling = (Oversampling) juce::jlimit (0, 2, (int) pOversampling->load());
    s.outputDb = pOutput->load();
    s.bypass = pBypass->load() > 0.5f;
    s.monitor = (Monitor) juce::jlimit (0, 3, (int) pMonitor->load());
    return s;
}

//==============================================================================
void BaxAudioProcessor::prepareToPlay (double sampleRate, int)
{
    displaySampleRate.store (sampleRate);
    engine.prepare (sampleRate, readSettings());   // builds the design tables (not real-time)
    setLatencySamples (BaxEngine::kLatency);
}

void BaxAudioProcessor::reset()
{
    engine.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0, readSettings());
}

bool BaxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

//==============================================================================
template <typename Sample>
void BaxAudioProcessor::processInternal (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);     // mono in -> both sides

    Sample* outL = buffer.getWritePointer (0);
    Sample* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    engine.setSettings (readSettings());

    for (int pos = 0; pos < n; pos += kChunk)
    {
        const int len = std::min (kChunk, n - pos);
        for (int i = 0; i < len; ++i)
        {
            scratchL[i] = (double) outL[pos + i];
            scratchR[i] = outR != nullptr ? (double) outR[pos + i] : scratchL[i];
        }
        engine.process (scratchL, scratchR, len);
        for (int i = 0; i < len; ++i)
        {
            outL[pos + i] = (Sample) scratchL[i];
            if (outR != nullptr) outR[pos + i] = (Sample) scratchR[i];
        }
    }
}

template <typename Sample>
void BaxAudioProcessor::bypassInternal (juce::AudioBuffer<Sample>& buffer)
{
    // Host bypass keeps the latency so the track stays in time.
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);

    Sample* outL = buffer.getWritePointer (0);
    Sample* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int pos = 0; pos < n; pos += kChunk)
    {
        const int len = std::min (kChunk, n - pos);
        for (int i = 0; i < len; ++i)
        {
            scratchL[i] = (double) outL[pos + i];
            scratchR[i] = outR != nullptr ? (double) outR[pos + i] : scratchL[i];
        }
        engine.processBypassed (scratchL, scratchR, len);
        for (int i = 0; i < len; ++i)
        {
            outL[pos + i] = (Sample) scratchL[i];
            if (outR != nullptr) outR[pos + i] = (Sample) scratchR[i];
        }
    }
}

void BaxAudioProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)          { processInternal (b); }
void BaxAudioProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&)         { processInternal (b); }
void BaxAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { bypassInternal (b); }
void BaxAudioProcessor::processBlockBypassed (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { bypassInternal (b); }

//==============================================================================
int BaxAudioProcessor::getNumPrograms()   { return numFactoryPresets; }

const juce::String BaxAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numFactoryPresets) ? juce::String (factoryPresets[index].name) : juce::String();
}

void BaxAudioProcessor::setParameterValue (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void BaxAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numFactoryPresets))
        return;

    currentProgram.store (index);
    const auto& p = factoryPresets[index];

    const PresetChannel* chans[2] = { &p.a, &p.b };
    for (int c = 0; c < 2; ++c)
    {
        const auto& ids = kChannelIDs[c];
        setParameterValue (ids.hp,     chans[c]->hp);
        setParameterValue (ids.lsFreq, chans[c]->lsFreq);
        setParameterValue (ids.lsGain, chans[c]->lsGain);
        setParameterValue (ids.hsGain, chans[c]->hsGain);
        setParameterValue (ids.hsFreq, chans[c]->hsFreq);
        setParameterValue (ids.lp,     chans[c]->lp);
        setParameterValue (ids.engage, 0.0f);
    }
    setParameterValue (ParamIDs::output, p.outputDb);
    setParameterValue (ParamIDs::midSide, p.midSide ? 1.0f : 0.0f);
    setParameterValue (ParamIDs::link, p.link ? 1.0f : 0.0f);
    setParameterValue (ParamIDs::oversampling, (float) p.oversampling);
    // channels with different frequency settings need the Dual Mono view
    if (! sameValue (p.a.hp, p.b.hp) || ! sameValue (p.a.lsFreq, p.b.lsFreq)
        || ! sameValue (p.a.hsFreq, p.b.hsFreq) || ! sameValue (p.a.lp, p.b.lp))
        setParameterValue (ParamIDs::view, (float) View::DualMono);
}

//==============================================================================
void BaxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("editorScale", editorScale.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BaxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram.store (juce::jlimit (0, numFactoryPresets - 1, (int) state.getProperty ("program", 0)));
            editorScale.store ((float) (double) state.getProperty ("editorScale", 1.0));
            apvts.replaceState (state);
        }
}

//==============================================================================
juce::AudioProcessorEditor* BaxAudioProcessor::createEditor()
{
    return new BaxAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BaxAudioProcessor();
}
