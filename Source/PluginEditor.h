#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class UbvEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UbvEditor (UbvProcessor&);
    ~UbvEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawBlock (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, const ubv::Judgement&, bool big);
    void drawHistogram (juce::Graphics&, juce::Rectangle<int>);

    using APVTS = juce::AudioProcessorValueTreeState;

    UbvProcessor& proc;
    juce::LookAndFeel_V4 lf;   // own colours, the shared default stays untouched

    juce::ComboBox role, grid;
    juce::Slider offset, sens, lo, hi;
    juce::ToggleButton resetOnPlay { "Reset on play" }, custom { "Custom pocket" };
    juce::TextButton reset { "Reset" };
    juce::Label roleLabel, gridLabel, offsetLabel, sensLabel, loLabel, hiLabel;

    std::unique_ptr<APVTS::ComboBoxAttachment> roleAtt, gridAtt;
    std::unique_ptr<APVTS::SliderAttachment> offsetAtt, sensAtt, loAtt, hiAtt;
    std::unique_ptr<APVTS::ButtonAttachment> resetOnPlayAtt, customAtt;

    // snapshot for painting
    std::array<uint32_t, ubv::kBins> hist {};
    ubv::Judgement total, recent;
    uint32_t outside = 0;
    ubv::Pocket pocket { -10.0f, 0.0f, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UbvEditor)
};
