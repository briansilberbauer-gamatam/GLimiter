#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/StudioLookAndFeel.h"

#include <memory>

class GLimiterEditorContent;

class GLimiterAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GLimiterAudioProcessorEditor (GLimiterAudioProcessor&);
    ~GLimiterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GLimiterAudioProcessor& processor;
    glimiter::ui::StudioLookAndFeel lookAndFeel;
    std::unique_ptr<GLimiterEditorContent> content;
    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GLimiterAudioProcessorEditor)
};
