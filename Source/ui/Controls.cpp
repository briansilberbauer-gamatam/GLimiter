#include "Controls.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace glimiter::ui
{

//==============================================================================
ParamKnob::ParamKnob (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& displayName,
                      Formatter formatter,
                      juce::Colour accent)
    : valueFormatter (std::move (formatter)),
      accentColour (accent),
      parameter (state.getParameter (parameterID))
{
    jassert (parameter != nullptr);

    nameLabel.setText (displayName, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (font (8.2f, FontWeight::semibold, 0.14f));
    nameLabel.setColour (juce::Label::textColourId, textSecondary);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (font (10.5f, FontWeight::semibold, 0.02f));
    valueLabel.setColour (juce::Label::textColourId, textPrimary.withAlpha (0.95f));
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.18f,
                                juce::MathConstants<float>::pi * 2.82f,
                                true);
    slider.setMouseDragSensitivity (190);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    slider.setColour (juce::Slider::thumbColourId, accentColour);
    slider.setTooltip (displayName);
    slider.setName (displayName);
    addAndMakeVisible (slider);

    slider.textFromValueFunction = [this] (double value)
    {
        return valueFormatter (static_cast<float> (value));
    };

    slider.onValueChange = [this] { updateValueLabel(); };

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);

    if (parameter != nullptr)
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    updateValueLabel();
}

void ParamKnob::setAccent (juce::Colour newAccent)
{
    accentColour = newAccent;
    slider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    slider.setColour (juce::Slider::thumbColourId, accentColour);
}

void ParamKnob::resized()
{
    auto area = getLocalBounds();
    area.reduce (1, 0);

    constexpr int nameHeight = 12;
    constexpr int valueHeight = 15;
    const int availableForKnob = juce::jmax (10, area.getHeight() - nameHeight - valueHeight);
    const int diameter = juce::jmax (10, juce::jmin (area.getWidth(), availableForKnob));

    auto knobArea = area.removeFromTop (availableForKnob);
    slider.setBounds (knobArea.withSizeKeepingCentre (diameter, diameter));

    nameLabel.setBounds (area.removeFromTop (nameHeight));
    valueLabel.setBounds (area.removeFromTop (valueHeight));
}

void ParamKnob::updateValueLabel()
{
    valueLabel.setText (valueFormatter (static_cast<float> (slider.getValue())),
                        juce::dontSendNotification);
}

//==============================================================================
ParamSlider::ParamSlider (juce::AudioProcessorValueTreeState& state,
                          const juce::String& parameterID,
                          const juce::String& displayName,
                          Formatter formatter)
    : valueFormatter (std::move (formatter)),
      parameter (state.getParameter (parameterID))
{
    jassert (parameter != nullptr);

    nameLabel.setText (displayName, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (font (8.2f, FontWeight::semibold, 0.16f));
    nameLabel.setColour (juce::Label::textColourId, textSecondary);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (font (9.0f, FontWeight::semibold, 0.02f));
    valueLabel.setColour (juce::Label::textColourId, textPrimary.withAlpha (0.95f));
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (170);
    slider.setTooltip (displayName);
    slider.setName (displayName);
    slider.setColour (juce::Slider::thumbColourId, crimsonAccent);
    slider.setColour (juce::Slider::trackColourId, crimsonAccent);
    addAndMakeVisible (slider);

    slider.textFromValueFunction = [this] (double value)
    {
        return valueFormatter (static_cast<float> (value));
    };

    slider.onValueChange = [this] { updateValueLabel(); };

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);

    if (parameter != nullptr)
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    updateValueLabel();
}

void ParamSlider::resized()
{
    auto area = getLocalBounds();
    nameLabel.setBounds (area.removeFromTop (22));
    valueLabel.setBounds (area.removeFromBottom (22));
    slider.setBounds (area.reduced (2, 1));
}

void ParamSlider::updateValueLabel()
{
    valueLabel.setText (valueFormatter (static_cast<float> (slider.getValue())),
                        juce::dontSendNotification);
}

//==============================================================================
PowerSwitch::PowerSwitch (juce::AudioProcessorValueTreeState& state,
                          const juce::String& parameterID,
                          juce::String buttonLabel,
                          std::function<void (bool)> onChanged)
    : juce::Button ("Power"),
      parameter (state.getParameter (parameterID)),
      changeCallback (std::move (onChanged)),
      label (std::move (buttonLabel))
{
    jassert (parameter != nullptr);

    setTooltip ("Enable or bypass this module");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setButtonText (label);

    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float value)
    {
        setOn (value > 0.5f);
    });

    attachment->sendInitialUpdate();
}

void PowerSwitch::clicked()
{
    setOn (! on);
    attachment->setValueAsCompleteGesture (on ? 1.0f : 0.0f);
}

void PowerSwitch::setOn (bool shouldBeOn)
{
    if (on == shouldBeOn)
        return;

    on = shouldBeOn;
    repaint();

    if (changeCallback != nullptr)
        changeCallback (on);
}

void PowerSwitch::paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    constexpr float corner = 3.5f;

    g.setColour (obsidianWell);
    g.fillRoundedRectangle (bounds.expanded (1.0f), corner + 0.8f);

    juce::ColourGradient capGradient (on ? gunmetalLight.interpolatedWith (crimsonGlow, 0.14f)
                                         : gunmetalBase,
                                      bounds.getTopLeft(),
                                      gunmetalDeep,
                                      bounds.getBottomRight(),
                                      false);
    g.setGradientFill (capGradient);
    g.fillRoundedRectangle (bounds, corner);

    if (isButtonDown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds, corner);
    }

    if (on)
    {
        g.setColour (crimsonGlow.withAlpha (0.34f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), corner + 0.5f, 2.8f);

        g.setColour (crimsonAccent);
        g.drawRoundedRectangle (bounds, corner, 1.2f);

        g.setColour (crimsonBright.withAlpha (0.72f));
        g.drawRoundedRectangle (bounds.reduced (0.4f), corner, 0.6f);

        g.setColour (crimsonGlow.withAlpha (0.10f));
        g.fillRoundedRectangle (bounds.reduced (1.2f), corner - 0.6f);
    }
    else
    {
        g.setColour (isMouseOverButton ? chamferLight : gunmetalLight.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }

    const auto dotCentre = juce::Point<float> (bounds.getX() + 10.0f, bounds.getCentreY());

    if (on)
    {
        drawCrimsonDot (g, dotCentre, 1.7f);
    }
    else
    {
        g.setColour (textMuted.withAlpha (0.55f));
        g.fillEllipse (dotCentre.x - 1.4f, dotCentre.y - 1.4f, 2.8f, 2.8f);
    }

    g.setFont (font (8.6f, on ? FontWeight::semibold : FontWeight::regular, 0.13f));
    g.setColour (on ? textPrimary : textSecondary);
    g.drawText (label.toUpperCase(),
                bounds.withTrimmedLeft (16.0f).withTrimmedRight (4.0f),
                juce::Justification::centred, false);
}

//==============================================================================
ModeBar::ModeBar (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  juce::String barTitle,
                  juce::StringArray choiceLabels)
    : parameter (state.getParameter (parameterID)),
      title (std::move (barTitle)),
      choices (std::move (choiceLabels))
{
    jassert (parameter != nullptr);
    jassert (! choices.isEmpty());
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float value)
    {
        setSelected (juce::roundToInt (value));
    });

    attachment->sendInitialUpdate();
}

juce::Rectangle<float> ModeBar::segmentBounds (int index) const
{
    const auto area = getLocalBounds().toFloat();
    const float gap = 4.0f;
    const int count = juce::jmax (1, choices.size());
    const float width = (area.getWidth() - gap * static_cast<float> (count - 1)) / static_cast<float> (count);
    const float x = static_cast<float> (index) * (width + gap);
    return { area.getX() + x, area.getY() + 13.0f, width, juce::jmax (15.0f, area.getHeight() - 13.0f) };
}

int ModeBar::indexAt (juce::Point<float> position) const
{
    for (int i = 0; i < choices.size(); ++i)
        if (segmentBounds (i).contains (position))
            return i;

    return -1;
}

void ModeBar::paint (juce::Graphics& g)
{
    g.setFont (font (7.2f, FontWeight::semibold, 0.16f));
    g.setColour (textSecondary);
    g.drawText (title, getLocalBounds().removeFromTop (11),
                juce::Justification::centredLeft, false);

    for (int i = 0; i < choices.size(); ++i)
    {
        const auto segment = segmentBounds (i);
        const bool active = i == selected;
        const bool isHovered = i == hovered;

        g.setColour (obsidianWell);
        g.fillRoundedRectangle (segment.expanded (0.8f), 3.2f);

        g.setColour (active ? gunmetalLight.interpolatedWith (crimsonGlow, 0.12f)
                            : (isHovered ? gunmetalBase.brighter (0.10f) : gunmetalBase));
        g.fillRoundedRectangle (segment, 2.4f);

        if (active)
        {
            g.setColour (crimsonGlow.withAlpha (0.32f));
            g.drawRoundedRectangle (segment.expanded (0.8f), 3.0f, 2.4f);
            g.setColour (crimsonAccent);
            g.drawRoundedRectangle (segment, 2.4f, 1.1f);
        }
        else
        {
            g.setColour (isHovered ? chamferLight : gunmetalLight.withAlpha (0.52f));
            g.drawRoundedRectangle (segment, 2.4f, 0.8f);
        }

        g.setFont (font (7.7f, active ? FontWeight::semibold : FontWeight::regular, 0.10f));
        g.setColour (active ? textPrimary : textSecondary);
        g.drawText (choices[i], segment, juce::Justification::centred, false);
    }
}

void ModeBar::mouseDown (const juce::MouseEvent& event)
{
    const int index = indexAt (event.position);

    if (index >= 0)
        attachment->setValueAsCompleteGesture (static_cast<float> (index));
}

void ModeBar::mouseMove (const juce::MouseEvent& event)
{
    const int newHovered = indexAt (event.position);

    if (newHovered != hovered)
    {
        hovered = newHovered;
        repaint();
    }
}

void ModeBar::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void ModeBar::setSelected (int newIndex)
{
    newIndex = juce::jlimit (0, choices.size() - 1, newIndex);

    if (newIndex != selected)
    {
        selected = newIndex;
        repaint();
    }
}

//==============================================================================
GainReductionMeter::GainReductionMeter()
{
    setInterceptsMouseClicks (false, false);
}

void GainReductionMeter::setReductionDb (float newReductionDb)
{
    const float target = juce::jlimit (0.0f, 24.0f, newReductionDb);

    if (target > displayedDb)
        displayedDb = target;
    else
        displayedDb += 0.22f * (target - displayedDb);

    if (displayedDb < 0.03f)
        displayedDb = 0.0f;

    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setFont (font (7.2f, FontWeight::semibold, 0.14f));
    g.setColour (textSecondary);
    g.drawText ("GAIN REDUCTION", area.removeFromTop (11.0f),
                juce::Justification::centredLeft, false);

    g.setColour (crimsonBright);
    g.setFont (font (8.2f, FontWeight::semibold, 0.02f));
    g.drawText (displayedDb > 0.05f ? "-" + juce::String (displayedDb, 1) + " dB" : "0.0 dB",
                getLocalBounds().toFloat().removeFromTop (11.0f),
                juce::Justification::centredRight, false);

    auto bar = getLocalBounds().toFloat().withTrimmedTop (15.0f).withHeight (6.0f);
    g.setColour (obsidianWell);
    g.fillRoundedRectangle (bar.expanded (0.5f), 3.0f);

    const float proportion = juce::jlimit (0.0f, 1.0f, displayedDb / 24.0f);

    if (proportion > 0.002f)
    {
        auto fill = bar.withWidth (juce::jmax (3.0f, bar.getWidth() * proportion));
        juce::ColourGradient grad (crimsonGlow, bar.getX(), bar.getY(),
                                   crimsonBright, bar.getRight(), bar.getY(), false);
        grad.addColour (0.55, crimsonAccent);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 3.0f);

        g.setColour (crimsonGlow.withAlpha (0.30f));
        g.drawRoundedRectangle (fill.expanded (1.0f), 3.5f, 1.4f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.40f));
    for (int i = 1; i < 6; ++i)
    {
        const float x = bar.getX() + bar.getWidth() * static_cast<float> (i) / 6.0f;
        g.drawVerticalLine (juce::roundToInt (x), bar.getY(), bar.getBottom());
    }

    g.setFont (font (6.4f, FontWeight::regular, 0.04f));
    g.setColour (textMuted);
    g.drawText ("0", getLocalBounds().toFloat().removeFromBottom (8.0f),
                juce::Justification::centredLeft, false);
    g.drawText ("24", getLocalBounds().toFloat().removeFromBottom (8.0f),
                juce::Justification::centredRight, false);
}

//==============================================================================
SweepMeter::SweepMeter()
{
    setInterceptsMouseClicks (false, false);
}

void SweepMeter::setCutoffHz (float newCutoffHz)
{
    const float target = juce::jlimit (80.0f, 8000.0f, newCutoffHz);
    displayedHz += 0.32f * (target - displayedHz);
    repaint();
}

void SweepMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setFont (font (7.2f, FontWeight::semibold, 0.14f));
    g.setColour (textSecondary);
    g.drawText ("ENVELOPE SWEEP", area.removeFromTop (11.0f),
                juce::Justification::centredLeft, false);

    g.setColour (crimsonBright);
    g.setFont (font (8.2f, FontWeight::semibold, 0.02f));
    g.drawText (formatHz (displayedHz), getLocalBounds().toFloat().removeFromTop (11.0f),
                juce::Justification::centredRight, false);

    auto bar = getLocalBounds().toFloat().withTrimmedTop (15.0f).withHeight (6.0f);
    g.setColour (obsidianWell);
    g.fillRoundedRectangle (bar.expanded (0.5f), 3.0f);

    const float normalised = juce::jlimit (0.0f, 1.0f,
        std::log (displayedHz / 80.0f) / std::log (8000.0f / 80.0f));

    juce::ColourGradient grad (crimsonGlow, bar.getX(), bar.getY(),
                               crimsonAccent, bar.getRight(), bar.getY(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar.withWidth (juce::jmax (4.0f, bar.getWidth() * normalised)), 3.0f);

    const float markerX = bar.getX() + bar.getWidth() * normalised;
    g.setColour (crimsonGlow.withAlpha (0.40f));
    g.fillRoundedRectangle (markerX - 2.0f, bar.getY() - 3.5f, 4.0f, 13.0f, 1.5f);
    g.setColour (textPrimary);
    g.fillRoundedRectangle (markerX - 0.8f, bar.getY() - 2.5f, 1.6f, 11.0f, 0.8f);

    g.setFont (font (6.4f, FontWeight::regular, 0.04f));
    g.setColour (textMuted);
    g.drawText ("80", getLocalBounds().toFloat().removeFromBottom (8.0f),
                juce::Justification::centredLeft, false);
    g.drawText ("8k", getLocalBounds().toFloat().removeFromBottom (8.0f),
                juce::Justification::centredRight, false);
}

//==============================================================================
StereoMeter::StereoMeter()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Output level - click to reset the clip indicator");
}

float StereoMeter::gainToDb (float level)
{
    return juce::jlimit (-60.0f, 3.0f, juce::Decibels::gainToDecibels (std::max (level, 1.0e-5f), -60.0f));
}

float StereoMeter::dbToNormalised (float db)
{
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

void StereoMeter::setLevels (float left, float right)
{
    const float targetL = gainToDb (left);
    const float targetR = gainToDb (right);

    // Fast attack, slow ballistic release, matching a hardware peak meter.
    displayLeftDb  = targetL > displayLeftDb  ? targetL  : juce::jmax (targetL,  displayLeftDb  - 1.4f);
    displayRightDb = targetR > displayRightDb ? targetR : juce::jmax (targetR, displayRightDb - 1.4f);

    const float samplePeak = juce::jmax (displayLeftDb, displayRightDb);

    if (samplePeak >= peakHoldDb)
    {
        peakHoldDb = samplePeak;
        peakHoldTicks = 32;
    }
    else if (--peakHoldTicks <= 0)
    {
        peakHoldDb = juce::jmax (samplePeak, peakHoldDb - 0.5f);
    }

    if (left > 1.0f || right > 1.0f)
        clipTicks = 45;
    else if (clipTicks > 0)
        --clipTicks;

    repaint();
}

void StereoMeter::mouseDown (const juce::MouseEvent&)
{
    clipTicks = 0;
    repaint();
}

void StereoMeter::paintBar (juce::Graphics& g, juce::Rectangle<float> track,
                            float levelDb, float peakDb) const
{
    g.setColour (obsidianWell);
    g.fillRoundedRectangle (track, 2.0f);

    const float fillW = dbToNormalised (levelDb) * track.getWidth();

    if (fillW > 0.5f)
    {
        const auto fill = track.withWidth (fillW);

        // Soft crimson outer bloom.
        g.setColour (crimsonGlow.withAlpha (0.32f));
        g.fillRoundedRectangle (fill.expanded (0.5f, 1.2f), 2.0f);

        // Muted crimson on the left, hot crimson near 0 dB on the right.
        juce::ColourGradient barGradient (crimsonAccent, track.getX(), fill.getCentreY(),
                                          crimsonBright, track.getRight(), fill.getCentreY(), false);
        g.setGradientFill (barGradient);
        g.fillRoundedRectangle (fill, 1.5f);

        g.setColour (crimsonBright);
        g.fillRect (fill.getRight() - 1.5f, fill.getY(), 1.5f, fill.getHeight());
    }

    // Ballistic peak-hold hairline.
    if (peakDb > -59.0f)
    {
        const float peakX = track.getX() + dbToNormalised (peakDb) * track.getWidth();
        g.setColour (crimsonBright.withAlpha (0.85f));
        g.fillRect (peakX - 0.6f, track.getY() - 1.0f, 1.2f, track.getHeight() + 2.0f);
    }
}

void StereoMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    // Recessed obsidian glass well.
    g.setColour (obsidianScreen);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (glassRimHighlight.withAlpha (0.18f));
    g.drawRoundedRectangle (bounds, 5.0f, 0.8f);
    g.setColour (crimsonAccent.withAlpha (0.28f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 4.2f, 0.6f);

    auto inner = bounds.reduced (8.0f, 5.0f);
    auto headerRow = inner.removeFromTop (14.0f);

    g.setColour (textSecondary);
    g.setFont (font (8.0f, FontWeight::semibold, 0.10f));
    g.drawText ("OUT METER", headerRow, juce::Justification::centredLeft, false);

    const bool clipping = clipTicks > 0;
    g.setColour (clipping ? crimsonBright
                          : (peakHoldDb > -0.5f ? crimsonBright : textPrimary));
    g.setFont (font (8.5f, FontWeight::semibold, 0.04f));
    g.drawText (peakHoldDb <= -59.5f ? juce::String ("-INF dB")
                                     : juce::String (peakHoldDb, 1) + " dB",
                headerRow, juce::Justification::centredRight, false);

    inner.removeFromTop (3.0f);
    const auto scaleRow = inner.removeFromBottom (11.0f);

    // Horizontal stereo L / R tracks: -60 dB on the left, 0 dB on the right.
    constexpr float labelW = 11.0f;
    const float barLeft = inner.getX() + labelW;
    const float barRight = inner.getRight() - 2.0f;
    const float barW = juce::jmax (20.0f, barRight - barLeft);
    constexpr float barH = 6.5f;
    const float midY = inner.getCentreY();

    const juce::Rectangle<float> leftTrack (barLeft, midY - barH - 1.5f, barW, barH);
    const juce::Rectangle<float> rightTrack (barLeft, midY + 1.5f, barW, barH);

    g.setColour (textMuted);
    g.setFont (font (7.5f, FontWeight::semibold));
    g.drawText ("L", juce::Rectangle<float> (inner.getX(), leftTrack.getY() - 1.0f,
                                             labelW - 2.0f, barH + 2.0f),
                juce::Justification::centredLeft, false);
    g.drawText ("R", juce::Rectangle<float> (inner.getX(), rightTrack.getY() - 1.0f,
                                             labelW - 2.0f, barH + 2.0f),
                juce::Justification::centredLeft, false);

    paintBar (g, leftTrack, displayLeftDb, peakHoldDb);
    paintBar (g, rightTrack, displayRightDb, peakHoldDb);

    // Minimalist dB scale along the bottom of the bars.
    constexpr float tickDbs[] { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f };
    g.setFont (font (7.0f, FontWeight::regular));

    for (float db : tickDbs)
    {
        const float x = barLeft + dbToNormalised (db) * barW;
        g.setColour (obsidianGrid.brighter (0.15f));
        g.fillRect (x - 0.5f, rightTrack.getBottom() + 1.0f, 1.0f, 2.5f);
        g.setColour (textMuted);
        g.drawText (juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (x - 10.0f, scaleRow.getY(), 20.0f, scaleRow.getHeight()),
                    juce::Justification::centred, false);
    }
}

//==============================================================================
PillNavButton::PillNavButton (bool pointsLeft)
    : juce::Button (pointsLeft ? "Previous preset" : "Next preset"),
      left (pointsLeft)
{
    setTooltip (pointsLeft ? "Previous factory preset" : "Next factory preset");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PillNavButton::paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    if (isMouseOverButton || isButtonDown)
    {
        g.setColour (juce::Colours::white.withAlpha (isButtonDown ? 0.06f : 0.03f));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (crimsonAccent.withAlpha (isButtonDown ? 0.55f : 0.25f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    const auto centre = bounds.getCentre();
    constexpr float size = 4.4f;
    const float direction = left ? -1.0f : 1.0f;

    juce::Path chevron;
    chevron.startNewSubPath (centre.x - direction * size * 0.55f, centre.y - size);
    chevron.lineTo (centre.x + direction * size * 0.55f, centre.y);
    chevron.lineTo (centre.x - direction * size * 0.55f, centre.y + size);

    g.setColour (isButtonDown ? crimsonBright
                              : (isMouseOverButton ? textPrimary : textSecondary));
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

//==============================================================================
PresetPill::PresetPill (GLimiterAudioProcessor& p)
    : processor (p)
{
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    presetBox.setTooltip ("Select a factory preset");
    presetBox.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    presetBox.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    presetBox.setColour (juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    presetBox.setColour (juce::ComboBox::textColourId, textPrimary);

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i).toUpperCase(), i + 1);

    lastPreset = processor.getCurrentPresetIndex();
    lastDirty = processor.isPresetDirty();
    presetBox.setSelectedId (lastPreset + 1, juce::dontSendNotification);

    presetBox.onChange = [this]
    {
        if (syncing)
            return;

        const int selected = presetBox.getSelectedId();

        if (selected > 0)
        {
            processor.setCurrentProgram (selected - 1);
            lastPreset = selected - 1;
            lastDirty = processor.isPresetDirty();
            repaint();
        }
    };

    addAndMakeVisible (presetBox);
}

void PresetPill::refresh()
{
    const int preset = processor.getCurrentPresetIndex();
    const bool dirty = processor.isPresetDirty();
    bool needsRepaint = false;

    if (preset != lastPreset)
    {
        syncing = true;
        presetBox.setSelectedId (preset + 1, juce::dontSendNotification);
        syncing = false;
        lastPreset = preset;
        needsRepaint = true;
    }

    if (dirty != lastDirty)
    {
        lastDirty = dirty;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void PresetPill::stepPreset (int direction)
{
    const int count = processor.getNumPrograms();

    if (count <= 0)
        return;

    const int current = processor.getCurrentPresetIndex();
    const int next = (current + direction + count) % count;
    processor.setCurrentProgram (next);

    syncing = true;
    presetBox.setSelectedId (next + 1, juce::dontSendNotification);
    syncing = false;
    lastPreset = next;
    lastDirty = processor.isPresetDirty();
    repaint();
}

void PresetPill::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = bounds.getHeight() * 0.5f;

    // Diffused crimson refraction around the recessed pill.
    g.setColour (crimsonGlow.withAlpha (0.24f));
    g.drawRoundedRectangle (bounds.expanded (1.5f), radius + 1.5f, 2.4f);

    g.setColour (obsidianScreen);
    g.fillRoundedRectangle (bounds, radius);

    // Subtle upper-left sheen across the obsidian readout.
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, radius);
        g.reduceClipRegion (clip);

        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.045f),
                                    bounds.getTopLeft(),
                                    juce::Colours::transparentWhite,
                                    bounds.getBottomLeft(), false);
        g.setGradientFill (sheen);
        g.fillRect (bounds.withHeight (bounds.getHeight() * 0.58f));
    }

    g.setColour (crimsonAccent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    g.setColour (glassRimHighlight.withAlpha (0.16f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), radius - 1.0f, 0.75f);

    const float navW = bounds.getWidth() * 1.0f / 10.2f;
    g.setColour (obsidianGrid);
    g.fillRect (bounds.getX() + navW, bounds.getY() + 7.0f, 1.0f, bounds.getHeight() - 14.0f);
    g.fillRect (bounds.getRight() - navW, bounds.getY() + 7.0f, 1.0f, bounds.getHeight() - 14.0f);

    // Tiny status line beneath the preset name.
    g.setFont (font (6.0f, FontWeight::semibold, 0.18f));
    g.setColour (lastDirty ? crimsonBright : textMuted);
    g.drawText (lastDirty ? "EDITED" : "FACTORY PRESET",
                bounds.withTrimmedTop (bounds.getHeight() - 13.0f).withHeight (10.0f),
                juce::Justification::centred, false);
}

void PresetPill::resized()
{
    const auto bounds = getLocalBounds();
    const int navW = juce::roundToInt (static_cast<float> (bounds.getWidth()) * 0.098f);

    prevButton.setBounds (5, 5, navW - 10, bounds.getHeight() - 10);
    nextButton.setBounds (bounds.getWidth() - navW + 5, 5, navW - 10, bounds.getHeight() - 10);
    presetBox.setBounds (navW, 4, bounds.getWidth() - navW * 2, 23);
}

} // namespace glimiter::ui
