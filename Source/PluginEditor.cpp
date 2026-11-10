#include "PluginEditor.h"

namespace
{
const juce::Colour bg     { 0xff17151f };
const juce::Colour panel  { 0xff221f2e };
const juce::Colour line   { 0xff3a3550 };
const juce::Colour text   { 0xffe6e1f0 };
const juce::Colour dim    { 0xff8d86a3 };
const juce::Colour accent { 0xffb9a3e3 };
const juce::Colour green  { 0xff6fcf7f };
const juce::Colour yellow { 0xffe8c95a };
const juce::Colour red    { 0xffe06c6c };

juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Colour scoreColour (float s)
{
    return s >= 80.0f ? green : s >= 60.0f ? yellow : red;
}

void setupLabel (juce::Label& l, const juce::String& t)
{
    l.setText (t, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, dim);
    l.setFont (font (13.0f));
    l.setJustificationType (juce::Justification::centredRight);
}
} // namespace

UbvEditor::UbvEditor (UbvProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lf);
    lf.setColour (juce::ComboBox::backgroundColourId, panel);
    lf.setColour (juce::ComboBox::outlineColourId, line);
    lf.setColour (juce::ComboBox::textColourId, text);
    lf.setColour (juce::ComboBox::arrowColourId, accent);
    lf.setColour (juce::PopupMenu::backgroundColourId, panel);
    lf.setColour (juce::PopupMenu::textColourId, text);
    lf.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));
    lf.setColour (juce::Slider::backgroundColourId, panel);
    lf.setColour (juce::Slider::trackColourId, accent.withAlpha (0.45f));
    lf.setColour (juce::Slider::textBoxTextColourId, text);
    lf.setColour (juce::Slider::textBoxOutlineColourId, line);
    lf.setColour (juce::ToggleButton::textColourId, text);
    lf.setColour (juce::ToggleButton::tickColourId, accent);
    lf.setColour (juce::ToggleButton::tickDisabledColourId, dim);
    lf.setColour (juce::TextButton::buttonColourId, panel);
    lf.setColour (juce::TextButton::textColourOffId, text);

    role.addItemList ({ "Drums (kit)", "Kick", "Overheads", "Bass", "Guitar" }, 1);
    grid.addItemList ({ "1/16", "1/16T", "1/8T", "1/32" }, 1);

    for (auto* s : { &offset, &sens, &lo, &hi })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
        addAndMakeVisible (*s);
    }
    offset.setTextValueSuffix (" ms");
    sens.setTextValueSuffix (" dB");
    lo.setTextValueSuffix (" ms");
    hi.setTextValueSuffix (" ms");

    setupLabel (roleLabel, "Role");
    setupLabel (gridLabel, "Grid");
    setupLabel (offsetLabel, "Input offset");
    setupLabel (sensLabel, "Sensitivity");
    setupLabel (loLabel, "from");
    setupLabel (hiLabel, "to");

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &role, &grid, &resetOnPlay, &custom, &reset,
             &roleLabel, &gridLabel, &offsetLabel, &sensLabel, &loLabel, &hiLabel })
        addAndMakeVisible (*c);

    reset.onClick = [this] { proc.shared.resetRequest.store (true); };

    auto& s = proc.apvts;
    roleAtt        = std::make_unique<APVTS::ComboBoxAttachment> (s, "role", role);
    gridAtt        = std::make_unique<APVTS::ComboBoxAttachment> (s, "grid", grid);
    offsetAtt      = std::make_unique<APVTS::SliderAttachment> (s, "offset", offset);
    sensAtt        = std::make_unique<APVTS::SliderAttachment> (s, "sens", sens);
    loAtt          = std::make_unique<APVTS::SliderAttachment> (s, "pocketLo", lo);
    hiAtt          = std::make_unique<APVTS::SliderAttachment> (s, "pocketHi", hi);
    resetOnPlayAtt = std::make_unique<APVTS::ButtonAttachment> (s, "resetOnPlay", resetOnPlay);
    customAtt      = std::make_unique<APVTS::ButtonAttachment> (s, "customPocket", custom);

    setSize (640, 470);
    timerCallback();
    startTimerHz (15);
}

UbvEditor::~UbvEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void UbvEditor::timerCallback()
{
    auto& sh = proc.shared;

    int64_t n = 0;
    for (int b = 0; b < ubv::kBins; ++b)
    {
        hist[(size_t) b] = sh.hist[(size_t) b].load (std::memory_order_relaxed);
        n += hist[(size_t) b];
    }
    outside = sh.outside.load (std::memory_order_relaxed);
    pocket = proc.currentPocket();

    float med = 0, sig = 0;
    ubv::medianSigmaHist (hist, n, med, sig);
    total = ubv::judge (med, sig, (int) n, pocket);

    // last bars: newest hits back while their bar is within the window
    std::vector<float> last;
    const auto cnt = sh.ringCount.load (std::memory_order_acquire);
    const int bar = sh.bar.load (std::memory_order_relaxed);
    for (uint32_t k = 0; k < std::min<uint32_t> (cnt, ubv::kRing); ++k)
    {
        const auto i = (cnt - 1 - k) % ubv::kRing;
        if (sh.ringBar[i].load (std::memory_order_relaxed) <= bar - ubv::kShortBars)
            break;
        last.push_back (sh.ringMs[i].load (std::memory_order_relaxed));
    }
    ubv::medianSigma (last, med, sig);
    recent = ubv::judge (med, sig, (int) last.size(), pocket);

    const bool c = custom.getToggleState();
    lo.setEnabled (c);
    hi.setEnabled (c);
    loLabel.setEnabled (c);
    hiLabel.setEnabled (c);

    repaint();
}

void UbvEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (40);   // title

    auto row = r.removeFromTop (28);
    roleLabel.setBounds (row.removeFromLeft (44));
    row.removeFromLeft (6);
    role.setBounds (row.removeFromLeft (130));
    gridLabel.setBounds (row.removeFromLeft (50));
    row.removeFromLeft (6);
    grid.setBounds (row.removeFromLeft (80));
    reset.setBounds (row.removeFromRight (80));
    row.removeFromRight (10);
    resetOnPlay.setBounds (row.removeFromRight (130));

    r.removeFromTop (8);
    row = r.removeFromTop (24);
    offsetLabel.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (6);
    offset.setBounds (row.removeFromLeft (190));
    sensLabel.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (6);
    sens.setBounds (row.removeFromLeft (190));

    r.removeFromTop (8);
    row = r.removeFromTop (24);
    custom.setBounds (row.removeFromLeft (140));
    loLabel.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (6);
    lo.setBounds (row.removeFromLeft (150));
    hiLabel.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (6);
    hi.setBounds (row.removeFromLeft (150));
}

void UbvEditor::drawBlock (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title,
                           const ubv::Judgement& j, bool big)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    r = r.reduced (12, 8);

    g.setColour (dim);
    g.setFont (font (12.0f, true));
    g.drawText (title, r.removeFromTop (16), juce::Justification::centredLeft);

    auto scoreArea = r.removeFromLeft (big ? 110 : 90);
    if (j.valid)
    {
        g.setColour (scoreColour (j.score));
        g.setFont (font (big ? 52.0f : 40.0f, true));
        g.drawText (juce::String (juce::roundToInt (j.score)), scoreArea, juce::Justification::centredLeft);

        g.setColour (text);
        g.setFont (font (big ? 22.0f : 18.0f, true));
        auto top = r.removeFromTop (r.getHeight() / 2);
        g.drawText (juce::String::formatted ("%+.1f ms", j.med), top, juce::Justification::bottomLeft);
        g.setColour (dim);
        g.setFont (font (14.0f));
        g.drawText (juce::String::formatted ("spread %.1f ms   hits %d", j.sig, j.n), r,
                    juce::Justification::topLeft);
    }
    else
    {
        g.setColour (dim);
        g.setFont (font (big ? 52.0f : 40.0f, true));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), scoreArea, juce::Justification::centredLeft);
        g.setFont (font (14.0f));
        g.drawText ("hits " + juce::String (j.n) + " (need " + juce::String (ubv::kMinNotes) + ")", r,
                    juce::Justification::centredLeft);
    }
}

void UbvEditor::drawHistogram (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    auto r = area.reduced (12, 10).toFloat();
    auto labels = r.removeFromBottom (16);

    auto xOf = [&] (float ms) { return r.getX() + (ms + ubv::kMaxOffMs) / (2.0f * ubv::kMaxOffMs) * r.getWidth(); };

    // pocket
    g.setColour (green.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (pocket.lo), r.getY(), xOf (pocket.hi), r.getBottom()));

    // grid lines every 10 ms
    for (int ms = -60; ms <= 60; ms += 10)
    {
        g.setColour (ms == 0 ? dim : line);
        g.drawVerticalLine (juce::roundToInt (xOf ((float) ms)), r.getY(), r.getBottom());
    }

    // bars: 2 ms groups of the 0.5 ms bins
    constexpr int group = 4;
    uint32_t peak = 1;
    std::vector<uint32_t> sums;
    for (int b = 0; b < ubv::kBins; b += group)
    {
        uint32_t s = 0;
        for (int k = b; k < std::min (b + group, ubv::kBins); ++k) s += hist[(size_t) k];
        sums.push_back (s);
        peak = std::max (peak, s);
    }
    const float bw = r.getWidth() / (float) sums.size();
    g.setColour (accent.withAlpha (0.8f));
    for (size_t i = 0; i < sums.size(); ++i)
    {
        const float h = r.getHeight() * (float) sums[i] / (float) peak;
        g.fillRect (r.getX() + (float) i * bw + 1.0f, r.getBottom() - h, std::max (1.0f, bw - 2.0f), h);
    }

    // medians: total (solid), last bars (white)
    if (total.valid)
    {
        g.setColour (scoreColour (total.score));
        g.fillRect (xOf (total.med) - 1.5f, r.getY(), 3.0f, r.getHeight());
    }
    if (recent.valid)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (xOf (recent.med) - 0.5f, r.getY(), 1.0f, r.getHeight());
    }

    g.setColour (dim);
    g.setFont (font (12.0f));
    g.drawText ("-60 ms  ahead", labels, juce::Justification::centredLeft);
    g.drawText ("0", labels.withX (xOf (0.0f) - 20.0f).withWidth (40.0f), juce::Justification::centred);
    g.drawText ("behind  +60 ms", labels, juce::Justification::centredRight);
}

void UbvEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto r = getLocalBounds().reduced (14);

    auto title = r.removeFromTop (32);
    g.setColour (accent);
    g.setFont (font (24.0f, true));
    g.drawText (proc.getName(), title, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (font (12.0f));
    g.drawText ("Insinistra", title, juce::Justification::centredRight);

    r.removeFromTop (8 + 28 + 8 + 24 + 8 + 24 + 12);   // controls (see resized)

    auto blocks = r.removeFromTop (100);
    auto left = blocks.removeFromLeft (blocks.getWidth() * 55 / 100);
    blocks.removeFromLeft (10);
    drawBlock (g, left, "TOTAL", total, true);
    drawBlock (g, blocks, "LAST " + juce::String (ubv::kShortBars) + " BARS", recent, false);

    r.removeFromTop (10);
    auto foot = r.removeFromBottom (16);
    r.removeFromBottom (4);
    drawHistogram (g, r);

    g.setColour (dim);
    g.setFont (font (12.0f));
    g.drawText ("outside the grid window: " + juce::String (outside), foot, juce::Justification::centredLeft);
    g.drawText ("+ behind / - ahead", foot, juce::Justification::centredRight);
}
