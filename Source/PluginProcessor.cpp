#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UbvName.h"

UbvProcessor::UbvProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "UBERVYSEKATEL", createLayout())
{
    roleParam        = apvts.getRawParameterValue ("role");
    gridParam        = apvts.getRawParameterValue ("grid");
    offsetParam      = apvts.getRawParameterValue ("offset");
    sensParam        = apvts.getRawParameterValue ("sens");
    resetOnPlayParam = apvts.getRawParameterValue ("resetOnPlay");
    customParam      = apvts.getRawParameterValue ("customPocket");
    loParam          = apvts.getRawParameterValue ("pocketLo");
    hiParam          = apvts.getRawParameterValue ("pocketHi");
}

const juce::String UbvProcessor::getName() const
{
    return juce::String (juce::CharPointer_UTF8 (UBV_DISPLAY_NAME));
}

juce::AudioProcessorValueTreeState::ParameterLayout UbvProcessor::createLayout()
{
    using namespace juce;
    auto ms = AudioParameterFloatAttributes().withLabel ("ms");
    auto db = AudioParameterFloatAttributes().withLabel ("dB");

    return {
        std::make_unique<AudioParameterChoice> (ParameterID { "role", 1 }, "Role",
                                                StringArray { "Drums (kit)", "Kick", "Overheads", "Bass", "Guitar" }, 0),
        std::make_unique<AudioParameterChoice> (ParameterID { "grid", 1 }, "Grid",
                                                StringArray { "1/16", "1/16T", "1/8T", "1/32" }, 0),
        std::make_unique<AudioParameterFloat> (ParameterID { "offset", 1 }, "Input offset",
                                               NormalisableRange<float> (0.0f, 200.0f, 0.1f), 0.0f, ms),
        std::make_unique<AudioParameterFloat> (ParameterID { "sens", 1 }, "Sensitivity",
                                               NormalisableRange<float> (-12.0f, 12.0f, 0.5f), 0.0f, db),
        std::make_unique<AudioParameterBool> (ParameterID { "resetOnPlay", 1 }, "Reset on play", false),
        std::make_unique<AudioParameterBool> (ParameterID { "customPocket", 1 }, "Custom pocket", false),
        std::make_unique<AudioParameterFloat> (ParameterID { "pocketLo", 1 }, "Pocket from",
                                               NormalisableRange<float> (-50.0f, 50.0f, 0.5f), -10.0f, ms),
        std::make_unique<AudioParameterFloat> (ParameterID { "pocketHi", 1 }, "Pocket to",
                                               NormalisableRange<float> (-50.0f, 50.0f, 0.5f), 0.0f, ms),
    };
}

ubv::Pocket UbvProcessor::currentPocket() const
{
    auto p = ubv::defaultPocket (currentRole());
    if (customParam->load() >= 0.5f)
    {
        p.lo = std::min (loParam->load(), hiParam->load());
        p.hi = std::max (loParam->load(), hiParam->load());
    }
    return p;
}

void UbvProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    detRole = -1;   // re-setup the detector on the next block
    detector.reset();
}

bool UbvProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in == layouts.getMainOutputChannelSet();
}

void UbvProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = getTotalNumInputChannels();

    for (int ch = numCh; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // transport and grid from the host
    bool playing = false;
    double ppq0 = 0.0, bpm = 120.0, barStart = 0.0;
    bool hostBar = false;
    int num = 4, den = 4;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto p = pos->getPpqPosition())
            {
                ppq0 = *p;
                playing = pos->getIsPlaying();
            }
            if (auto b = pos->getBpm(); b && *b > 1.0) bpm = *b;
            if (auto ts = pos->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0)
            {
                num = ts->numerator;
                den = ts->denominator;
            }
            if (auto bs = pos->getPpqPositionOfLastBarStart())
            {
                barStart = *bs;
                hostBar = true;
            }
        }
    }
    if (! hostBar)
    {
        const double barLen = 4.0 * num / den;
        barStart = std::floor (ppq0 / barLen) * barLen;
    }

    if (shared.resetRequest.exchange (false))
        shared.clear();
    if (playing && ! wasPlaying && resetOnPlayParam->load() >= 0.5f)
        shared.clear();

    if (playing)
    {
        if (! haveBar || std::abs (barStart - lastBarStart) > 1e-6)
            shared.bar.fetch_add (1, std::memory_order_relaxed);
        haveBar = true;
        lastBarStart = barStart;
    }
    else
    {
        haveBar = false;
    }
    wasPlaying = playing;

    const int role = (int) roleParam->load();
    const float sens = sensParam->load();
    if (role != detRole || sens != detSens)
    {
        detector.setup (role, sens, sr);
        detRole = role;
        detSens = sens;
    }

    const double ppqPerSample = bpm / 60.0 / sr;
    const double offsetPpq = offsetParam->load() / 1000.0 * bpm / 60.0;
    const double step = ubv::gridStep ((int) gridParam->load());
    const double stepMs = step * 60.0 / bpm * 1000.0;
    const float maxMs = (float) std::min ((double) ubv::kMaxOffMs, ubv::kMaxFrac * stepMs);
    const int bar = shared.bar.load (std::memory_order_relaxed);

    const float* l = buffer.getReadPointer (0);
    const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = 0.5f * (l[i] + r[i]);
        if (! detector.process (x) || ! playing)
            continue;

        // live input reaches the plugin late by the interface round trip: Input offset
        const double rel = ppq0 + i * ppqPerSample - offsetPpq - barStart;
        const double g = std::round (rel / step) * step;
        const float ms = (float) ((rel - g) * 60.0 / bpm * 1000.0);

        if (std::abs (ms) <= maxMs)
            shared.addHit (ms, bar);
        else
            shared.outside.fetch_add (1, std::memory_order_relaxed);
    }
    // audio passes through unchanged
}

void UbvProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void UbvProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* UbvProcessor::createEditor()
{
    return new UbvEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UbvProcessor();
}
