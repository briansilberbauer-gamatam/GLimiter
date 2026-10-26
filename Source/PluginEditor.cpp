#include "PluginEditor.h"

#include "Parameters.h"
#include "Version.h"
#include "ui/Controls.h"
#include "ui/EffectPanel.h"
#include "ui/Theme.h"
#include "ui/WaveformDisplay.h"

#include <array>
#include <cmath>
#include <utility>

using namespace glimiter::ui;

namespace
{
constexpr int designWidth = 1180;
constexpr int designHeight = 720;

void drawPickMark (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float x = area.getX();
    const float y = area.getY();
    const float w = area.getWidth();
    const float h = area.getHeight();
    const float cx = area.getCentreX();

    juce::Path pick;
    pick.startNewSubPath (cx, y);
    pick.cubicTo (x + w, y, x + w, y + h * 0.34f, x + w * 0.88f, y + h * 0.60f);
    pick.cubicTo (x + w * 0.78f, y + h * 0.84f, cx + w * 0.14f, y + h, cx, y + h);
    pick.cubicTo (cx - w * 0.14f, y + h, x + w * 0.22f, y + h * 0.84f, x + w * 0.12f, y + h * 0.60f);
    pick.cubicTo (x, y + h * 0.34f, x, y, cx, y);
    pick.closeSubPath();

    g.setColour (crimsonGlow.withAlpha (0.30f));
    g.fillPath (pick, juce::AffineTransform::scale (1.10f, 1.06f, cx, area.getCentreY()));

    juce::ColourGradient metal (gunmetalLight.brighter (0.10f), area.getTopLeft(),
                                gunmetalDeep, area.getBottomRight(), false);
    metal.addColour (0.45f, gunmetalBase);
    g.setGradientFill (metal);
    g.fillPath (pick);

    g.setColour (crimsonAccent.withAlpha (0.90f));
    g.strokePath (pick, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    g.setColour (glassRimHighlight.withAlpha (0.30f));
    juce::Path inner = pick;
    inner.applyTransform (juce::AffineTransform::scale (0.84f, 0.84f, cx, area.getCentreY()));
    g.strokePath (inner, juce::PathStrokeType (0.7f));

    // A tiny three-string motif etched into the pick face.
    for (int i = 0; i < 3; ++i)
    {
        const float yy = y + h * (0.34f + 0.11f * static_cast<float> (i));
        const float half = w * (0.20f - 0.035f * static_cast<float> (i));
        g.setColour (crimsonBright.withAlpha (0.55f - 0.10f * static_cast<float> (i)));
        g.fillRect (cx - half, yy, half * 2.0f, 0.9f);
    }
}
} // namespace

class GLimiterEditorContent final : public juce::Component,
                                    private juce::Timer
{
public:
    explicit GLimiterEditorContent (GLimiterAudioProcessor& p)
        : processor (p),
          apvts (p.getValueTreeState())
    {
        setOpaque (true);

        limiterPanel = std::make_unique<EffectPanel> (apvts, "01", "LIMITER",
                                                      "PICK-PRESERVING BRICKWALL / 2 MS LOOKAHEAD",
                                                      crimsonAccent, glimiter::params::limiterOn,
                                                      3, "LIMIT");
        addAndMakeVisible (*limiterPanel);

        shapePanel = std::make_unique<EffectPanel> (apvts, "02", "GUITAR SHAPE",
                                                    "BODY / AIR / BITE / PARALLEL MIX",
                                                    crimsonAccent, glimiter::params::shapeOn,
                                                    2, "SHAPE");
        addAndMakeVisible (*shapePanel);

        limiterPanel->addKnob ("THRESHOLD", glimiter::params::threshold, formatDb);
        limiterPanel->addKnob ("CEILING", glimiter::params::ceiling, formatDb);
        limiterPanel->addKnob ("ATTACK", glimiter::params::attack, formatTimeMs);
        limiterPanel->addKnob ("RELEASE", glimiter::params::release, formatTimeMs);
        limiterPanel->addKnob ("KNEE", glimiter::params::knee, formatDb);
        limiterPanel->addKnob ("THUMP", glimiter::params::detectorHpf, formatHz);

        shapePanel->addKnob ("BODY", glimiter::params::body, formatDb);
        shapePanel->addKnob ("AIR", glimiter::params::air, formatDb);
        shapePanel->addKnob ("BITE", glimiter::params::bite, formatPercent);
        shapePanel->addKnob ("MIX", glimiter::params::mix, formatPercent);

        gainReductionMeter = new GainReductionMeter();
        limiterPanel->setBottomComponent (std::unique_ptr<GainReductionMeter> (gainReductionMeter), 34);

        releaseModeBar = new ModeBar (apvts, glimiter::params::autoRelease,
                                      "RELEASE PROGRAMME",
                                      juce::StringArray { "MANUAL", "AUTO" });
        shapePanel->setBottomComponent (std::unique_ptr<ModeBar> (releaseModeBar), 34);

        inputSlider = std::make_unique<ParamSlider> (apvts, glimiter::params::inputGain,
                                                     "INPUT", formatDb);
        addAndMakeVisible (*inputSlider);

        outputSlider = std::make_unique<ParamSlider> (apvts, glimiter::params::outputGain,
                                                      "OUTPUT", formatDb);
        addAndMakeVisible (*outputSlider);

        addAndMakeVisible (outputMeter);

        bypassSwitch = std::make_unique<PowerSwitch> (apvts, glimiter::params::bypass, "BYPASS");
        addAndMakeVisible (*bypassSwitch);

        waveform = std::make_unique<WaveformDisplay> (processor);
        addAndMakeVisible (*waveform);

        presetPill = std::make_unique<PresetPill> (processor);
        addAndMakeVisible (*presetPill);

        startTimerHz (30);
    }

    ~GLimiterEditorContent() override
    {
        stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        drawDesktopHardwareChassis (g, getLocalBounds().toFloat());

        paintHeader (g);
        paintFooter (g);
    }

    void resized() override
    {
        presetPill->setBounds (375, 30, 430, 44);

        waveform->setBounds (30, 86, 1118, 136);

        limiterPanel->setBounds (30, 236, 700, 352);
        shapePanel->setBounds (746, 236, 402, 352);

        inputSlider->setBounds (44, 606, 58, 80);
        outputSlider->setBounds (108, 606, 58, 80);
        outputMeter.setBounds (196, 612, 320, 66);
        bypassSwitch->setBounds (982, 630, 138, 30);
    }

private:
    void paintHeader (juce::Graphics& g)
    {
        drawPickMark (g, { 40.0f, 27.0f, 42.0f, 49.0f });

        g.setColour (textPrimary);
        g.setFont (font (25.0f, FontWeight::black, 0.03f));
        g.drawText ("GLIMITER", 94, 28, 300, 30, juce::Justification::centredLeft, false);

        g.setColour (textSecondary);
        g.setFont (font (7.0f, FontWeight::semibold, 0.17f));
        g.drawText ("GUITAR LIMITER  /  PICK-PRESERVING DYNAMICS",
                    95, 57, 330, 10, juce::Justification::centredLeft, false);

        g.setColour (textSecondary.withAlpha (0.62f));
        g.setFont (font (6.2f, FontWeight::semibold, 0.16f));
        g.drawText ("THUMP-FILTERED DETECTOR  /  BODY + AIR + BITE",
                    95, 68, 330, 9, juce::Justification::centredLeft, false);

        g.setColour (textPrimary.withAlpha (0.90f));
        g.setFont (font (7.2f, FontWeight::black, 0.18f));
        g.drawText ("BLS AUDIO", 930, 36, 176, 10,
                    juce::Justification::centredRight, false);

        g.setColour (textMuted);
        g.setFont (font (6.2f, FontWeight::semibold, 0.14f));
        g.drawText ("64-BIT VST3  /  v" + juce::String (glimiter::version),
                    930, 49, 176, 9, juce::Justification::centredRight, false);
        g.setColour (textSecondary.withAlpha (0.58f));
        g.drawText ("SERIES GL-1  /  GUITAR DYNAMICS", 930, 62, 176, 9,
                    juce::Justification::centredRight, false);
    }

    void paintFooter (juce::Graphics& g)
    {
        const auto footer = juce::Rectangle<float> (30.0f, 600.0f, 1118.0f, 88.0f);

        drawGlassWell (g, footer, 7.0f);

        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.025f), footer.getTopLeft(),
                                    juce::Colours::transparentWhite, footer.getBottomLeft(), false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (footer, 7.0f);

        g.setColour (glassRimHighlight.withAlpha (0.14f));
        g.fillRect (footer.getX() + 122.0f, footer.getY() + 11.0f, 1.0f, footer.getHeight() - 22.0f);
        g.fillRect (footer.getX() + 500.0f, footer.getY() + 11.0f, 1.0f, footer.getHeight() - 22.0f);

        g.setColour (textMuted);
        g.setFont (font (6.4f, FontWeight::semibold, 0.17f));
        g.drawText ("GUITAR SIGNAL PATH", 548, 608, 260, 10,
                    juce::Justification::centredLeft, false);

        const std::array<const char*, 5> stages { "INPUT", "SHAPE", "LIMIT", "CEIL", "OUT" };

        float chipX = 548.0f;

        for (size_t i = 0; i < stages.size(); ++i)
        {
            const auto chip = juce::Rectangle<float> (chipX, 624.0f, 66.0f, 19.0f);
            drawSignalChip (g, chip, stages[i]);

            if (i + 1 < stages.size())
            {
                g.setColour (crimsonAccent.withAlpha (0.72f));
                g.setFont (font (8.0f, FontWeight::black));
                g.drawText (">", juce::Rectangle<float> (chip.getRight(), chip.getY(),
                                                         10.0f, chip.getHeight()),
                            juce::Justification::centred, false);
            }

            chipX += 76.0f;
        }

        g.setColour (textMuted);
        g.setFont (font (6.0f, FontWeight::semibold, 0.13f));
        g.drawText ("THUMP FILTER 20-400 HZ  /  PROGRAMME-DEPENDENT RELEASE",
                    548, 650, 390, 9, juce::Justification::centredLeft, false);
        g.drawText ("FIXED 2.0 MS LOOKAHEAD  /  DELAY-COMPENSATED BYPASS",
                    548, 663, 390, 9, juce::Justification::centredLeft, false);

        g.setColour (textMuted);
        g.setFont (font (6.4f, FontWeight::semibold, 0.16f));
        g.drawText ("MASTER", 982, 608, 138, 10, juce::Justification::centredLeft, false);

        g.setColour (textMuted.withAlpha (0.72f));
        g.setFont (font (5.8f, FontWeight::regular, 0.10f));
        g.drawText ("RAW INPUT / LATENCY ALIGNED", 982, 664, 138, 9,
                    juce::Justification::centredLeft, false);
    }

    void timerCallback() override
    {
        outputMeter.setLevels (processor.getOutputLevelLeft(), processor.getOutputLevelRight());

        if (gainReductionMeter != nullptr)
            gainReductionMeter->setReductionDb (processor.getGainReductionDb());

        if (presetPill != nullptr)
            presetPill->refresh();
    }

    GLimiterAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    std::unique_ptr<EffectPanel> limiterPanel;
    std::unique_ptr<EffectPanel> shapePanel;

    std::unique_ptr<ParamSlider> inputSlider;
    std::unique_ptr<ParamSlider> outputSlider;
    StereoMeter outputMeter;

    std::unique_ptr<PowerSwitch> bypassSwitch;
    std::unique_ptr<WaveformDisplay> waveform;
    std::unique_ptr<PresetPill> presetPill;

    GainReductionMeter* gainReductionMeter = nullptr;
    ModeBar* releaseModeBar = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GLimiterEditorContent)
};

//==============================================================================
GLimiterAudioProcessorEditor::GLimiterAudioProcessorEditor (GLimiterAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    content = std::make_unique<GLimiterEditorContent> (processor);
    addAndMakeVisible (*content);

    setResizable (true, true);
    setResizeLimits (885, 540, 1770, 1080);

    if (auto* boundsConstrainer = getConstrainer())
        boundsConstrainer->setFixedAspectRatio (static_cast<double> (designWidth) / static_cast<double> (designHeight));

    setSize (designWidth, designHeight);
}

GLimiterAudioProcessorEditor::~GLimiterAudioProcessorEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void GLimiterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (studioSurfaceDark);
}

void GLimiterAudioProcessorEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = juce::jmin (static_cast<float> (getWidth()) / static_cast<float> (designWidth),
                                    static_cast<float> (getHeight()) / static_cast<float> (designHeight));
    const float offsetX = (static_cast<float> (getWidth()) - static_cast<float> (designWidth) * scale) * 0.5f;
    const float offsetY = (static_cast<float> (getHeight()) - static_cast<float> (designHeight) * scale) * 0.5f;

    content->setBounds (0, 0, designWidth, designHeight);
    content->setTransform (juce::AffineTransform::scale (scale).translated (offsetX, offsetY));
}
