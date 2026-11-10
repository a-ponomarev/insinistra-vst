#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Timing.h"

class UbvProcessor final : public juce::AudioProcessor
{
public:
    UbvProcessor();

    const juce::String getName() const override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // pocket in force: the role default, or the custom one
    ubv::Pocket currentPocket() const;
    int currentRole() const { return (int) roleParam->load(); }

    juce::AudioProcessorValueTreeState apvts;
    ubv::Shared shared;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* roleParam = nullptr;
    std::atomic<float>* gridParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* sensParam = nullptr;
    std::atomic<float>* resetOnPlayParam = nullptr;
    std::atomic<float>* customParam = nullptr;
    std::atomic<float>* loParam = nullptr;
    std::atomic<float>* hiParam = nullptr;

    ubv::Detector detector;
    double sr = 44100.0;
    int detRole = -1;
    float detSens = 1000.0f;
    bool wasPlaying = false;
    bool haveBar = false;
    double lastBarStart = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UbvProcessor)
};
