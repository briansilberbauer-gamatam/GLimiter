#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

class GLimiterAudioProcessor;

namespace glimiter::ui
{

// Central obsidian visualizer: dB/time grid plus a glowing crimson live waveform.
class WaveformDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    explicit WaveformDisplay (GLimiterAudioProcessor&);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    GLimiterAudioProcessor& processor;
    static constexpr int numColumns = 512;
    std::array<float, numColumns> samples {};
    float displayGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};

} // namespace glimiter::ui
