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
                                                    "BODY / AIR / BITE / PICK DYNAMICS",
                                                    crimsonAccent, glimiter::params::shapeOn,
                                                    3, "SHAPE");
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

        gainReductionMeter = new GainReductionMeter();
        limiterPanel->setBottomComponent (std::unique_ptr<GainReductionMeter> (gainReductionMeter), 34);

        releaseModeBar = new ModeBar (apvts, glimiter::params::autoRelease,
                                      "RELEASE PROGRAMME",
                                      juce::StringArray { "MANUAL", "AUTO" });
        shapePanel->setBottomComponent (std::unique_ptr<ModeBar> (releaseModeBar), 34);

        inputSlider = std::make_unique<ParamSlider> (apvts, glimiter::params::inputGain,
                                                     "INPUT", formatDb);
        addAndMakeVisible (*inputSlider);

        mixSlider = std::make_unique<ParamSlider> (apvts, glimiter::params::mix,
                                                   "MIX", formatPercent);
        addAndMakeVisible (*mixSlider);

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
        drawRightSideMixerBay (g);

        paintHeader (g);
        paintFooter (g);
    }

    void resized() override
    {
        // Match drawDesktopHardwareChassis() exactly: unit -> metal base -> glass -> interior.
        auto unitBounds  = getLocalBounds().reduced (18, 16);
        auto metalBase   = unitBounds.reduced (5);
        auto glassPanel  = metalBase.reduced (6);
        auto innerGlass  = glassPanel.reduced (18, 16);

        // 1. Carve the right-side Mixer Bay off FIRST so it spans the full interior height.
        constexpr int mixerBayWidth = 204;
        auto rightMixerBay = innerGlass.removeFromRight (mixerBayWidth);
        mixerBayBounds = rightMixerBay.toFloat();
        innerGlass.removeFromRight (14); // Divider hairline + gap clearance.

        // 1a. Horizontal Output Meter, vertically BELOW the three mixer sliders.
        auto outputMeterArea = rightMixerBay.removeFromBottom (72);
        outputMeter.setBounds (outputMeterArea.reduced (6, 4));
        rightMixerBay.removeFromBottom (8);

        // 1b. Input / Wet-Dry Mix / Output vertical columns across the upper mixer bay.
        ParamSlider* mixers[3] { inputSlider.get(), mixSlider.get(), outputSlider.get() };
        const int colWidth = rightMixerBay.getWidth() / 3;

        for (int i = 0; i < 3; ++i)
        {
            auto column = rightMixerBay.removeFromLeft (i == 2 ? rightMixerBay.getWidth() : colWidth)
                                       .reduced (4, 0);
            mixers[i]->setBounds (column);
        }

        // 2. Main workspace: pill header, central waveform, module panels, tactile footer row.
        const int left   = innerGlass.getX();
        const int right  = innerGlass.getRight();
        const int width  = innerGlass.getWidth();

        presetPill->setBounds (390, 30, 380, 42);
        waveform->setBounds (left, 84, width, 156);

        constexpr int panelTop = 252;
        constexpr int panelHeight = 372;
        constexpr int panelGap = 16;
        const int limiterWidth = 528;

        limiterPanel->setBounds (left, panelTop, limiterWidth, panelHeight);
        shapePanel->setBounds (left + limiterWidth + panelGap, panelTop,
                               right - (left + limiterWidth + panelGap), panelHeight);

        bypassSwitch->setBounds (right - 122, 638, 122, 30);
    }

private:
    void drawRightSideMixerBay (juce::Graphics& g) const
    {
        if (mixerBayBounds.isEmpty())
            return;

        // Smoked glass recess spanning the full interior height on the right.
        g.setColour (obsidianScreen.withAlpha (0.28f));
        g.fillRoundedRectangle (mixerBayBounds, 6.0f);

        // Full-height divider hairline with a subtle crimson edge refraction.
        const float divX = mixerBayBounds.getX() - 7.0f;
        g.setColour (obsidianGrid);
        g.fillRect (divX, mixerBayBounds.getY() + 4.0f, 1.0f, mixerBayBounds.getHeight() - 8.0f);

        juce::ColourGradient dividerGlow (juce::Colours::transparentBlack, divX, mixerBayBounds.getY(),
                                          juce::Colours::transparentBlack, divX, mixerBayBounds.getBottom(),
                                          false);
        dividerGlow.addColour (0.5, crimsonAccent.withAlpha (0.32f));
        g.setGradientFill (dividerGlow);
        g.fillRect (divX + 1.0f, mixerBayBounds.getY() + 12.0f, 1.0f,
                    mixerBayBounds.getHeight() - 24.0f);
    }

    void paintHeader (juce::Graphics& g)
    {
        drawPickMark (g, { 40.0f, 27.0f, 42.0f, 49.0f });

        g.setColour (textPrimary);
        g.setFont (font (25.0f, FontWeight::black, 0.03f));
        g.drawText ("GLIMITER", 94, 28, 300, 30, juce::Justification::centredLeft, false);

        g.setColour (textSecondary);
        g.setFont (font (7.0f, FontWeight::semibold, 0.17f));
        g.drawText ("GUITAR LIMITER  /  PICK-PRESERVING DYNAMICS",
                    95, 57, 290, 10, juce::Justification::centredLeft, false);

        g.setColour (textSecondary.withAlpha (0.62f));
        g.setFont (font (6.2f, FontWeight::semibold, 0.16f));
        g.drawText ("THUMP-FILTERED DETECTOR  /  BODY + AIR + BITE",
                    95, 68, 290, 9, juce::Justification::centredLeft, false);

        g.setColour (textPrimary.withAlpha (0.90f));
        g.setFont (font (7.2f, FontWeight::black, 0.18f));
        g.drawText ("BLS AUDIO", 790, 36, 126, 10,
                    juce::Justification::centredRight, false);

        g.setColour (textMuted);
        g.setFont (font (6.2f, FontWeight::semibold, 0.14f));
        g.drawText ("64-BIT VST3  /  v" + juce::String (glimiter::version),
                    790, 49, 126, 9, juce::Justification::centredRight, false);
        g.setColour (textSecondary.withAlpha (0.58f));
        g.drawText ("SERIES GL-1  /  GUITAR DYNAMICS", 790, 62, 126, 9,
                    juce::Justification::centredRight, false);
    }

    void paintFooter (juce::Graphics& g)
    {
        const auto strip = juce::Rectangle<float> (47.0f, 632.0f, 868.0f, 44.0f);

        drawGlassWell (g, strip, 6.0f);

        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.025f), strip.getTopLeft(),
                                    juce::Colours::transparentWhite, strip.getBottomLeft(), false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (strip, 6.0f);

        g.setColour (textMuted);
        g.setFont (font (6.4f, FontWeight::semibold, 0.17f));
        g.drawText ("SIGNAL PATH", 58, 641, 80, 10,
                    juce::Justification::centredLeft, false);

        const std::array<const char*, 5> stages { "INPUT", "SHAPE", "LIMIT", "CEIL", "OUT" };

        float chipX = 142.0f;

        for (size_t i = 0; i < stages.size(); ++i)
        {
            const auto chip = juce::Rectangle<float> (chipX, 643.0f, 64.0f, 21.0f);
            drawSignalChip (g, chip, stages[i]);

            if (i + 1 < stages.size())
            {
                g.setColour (crimsonAccent.withAlpha (0.72f));
                g.setFont (font (8.0f, FontWeight::black));
                g.drawText (">", juce::Rectangle<float> (chip.getRight(), chip.getY(),
                                                         9.0f, chip.getHeight()),
                            juce::Justification::centred, false);
            }

            chipX += 73.0f;
        }

        g.setColour (textMuted);
        g.setFont (font (6.0f, FontWeight::semibold, 0.13f));
        g.drawText ("FIXED 2.0 MS LOOKAHEAD", 524, 641, 220, 9,
                    juce::Justification::centredLeft, false);
        g.drawText ("DELAY-COMPENSATED BYPASS", 524, 653, 220, 9,
                    juce::Justification::centredLeft, false);

        g.setColour (glassRimHighlight.withAlpha (0.14f));
        g.fillRect (780.0f, 639.0f, 1.0f, 30.0f);

        g.setColour (textMuted);
        g.setFont (font (6.0f, FontWeight::semibold, 0.14f));
        g.drawText ("MASTER", 690, 641, 78, 10,
                    juce::Justification::centredRight, false);
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
    std::unique_ptr<ParamSlider> mixSlider;
    std::unique_ptr<ParamSlider> outputSlider;
    StereoMeter outputMeter;
    juce::Rectangle<float> mixerBayBounds;

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
