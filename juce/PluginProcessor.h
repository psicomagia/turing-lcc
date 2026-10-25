#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "tm.h"

class TmProcessor : public juce::AudioProcessor
{
public:
    TmProcessor();
    ~TmProcessor() override;

    void prepareToPlay (double sampleRate, int) override { tm_set_rate (core, sampleRate); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto& o = l.getMainOutputChannelSet();
        return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Turing LCC"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // chamada da página (thread da interface): {m, path, q, body} -> {status, type, body}
    juce::var http (const juce::String& json);

private:
    TmCore* core = nullptr;
    std::vector<TmMidi> in, out;
    std::vector<float> scratch;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TmProcessor)
};
