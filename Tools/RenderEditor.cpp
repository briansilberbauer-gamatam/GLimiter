#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Version.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>
#include <iostream>
#include <memory>

namespace
{
// A plucked, palm-muted guitar-ish test signal so the visualiser has real
// pick transients and a decaying harmonic body in the screenshot.
float guitarSample (juce::int64 sample, double sampleRate)
{
    // Alternating low-string notes with a real pick transient, so the captured
    // frame shows both the harmonic body and the limiter clamping the attack.
    constexpr double notePeriod = 0.62;
    static constexpr double noteHz[] { 82.41, 98.00, 110.00, 146.83,
                                       130.81, 110.00, 98.00, 146.83 };

    const double t = static_cast<double> (sample) / sampleRate;
    const int note = static_cast<int> (t / notePeriod);
    const double noteTime = std::fmod (t, notePeriod);
    const double frequency = noteHz[static_cast<size_t> (note % 8)];

    const double attack = juce::jmin (1.0, noteTime / 0.004);
    const double decay = std::exp (-noteTime * 1.9);
    const double palm = 0.60 + 0.40 * std::exp (-noteTime * 7.0);
    const double phase = juce::MathConstants<double>::twoPi
                       * frequency * t
                       * (1.0 + 0.002 * std::sin (juce::MathConstants<double>::twoPi * 0.7 * t));

    double body = 0.0;

    for (int harmonic = 1; harmonic <= 10; ++harmonic)
    {
        const double rolloff = std::pow (0.78, static_cast<double> (harmonic - 1));
        body += rolloff * std::sin (phase * harmonic + harmonic * 0.42)
                / static_cast<double> (harmonic);
    }

    const double pick = 0.40 * std::exp (-noteTime * 48.0)
                      * std::sin (juce::MathConstants<double>::twoPi * 3800.0 * t)
                      * palm;

    return static_cast<float> (2.55 * attack * decay * (0.62 * body + pick));
}
} // namespace

class GLimiterRenderApplication final : public juce::JUCEApplication,
                                        private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "GLimiter Screenshot"; }
    const juce::String getApplicationVersion() override { return glimiter::version; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void systemRequestedQuit() override {}
    void shutdown() override {}

    void initialise (const juce::String& commandLine) override
    {
        outputFile = juce::File::getCurrentWorkingDirectory().getChildFile ("glimiter-ui.png");

        for (const auto& argument : juce::StringArray::fromTokens (commandLine, true))
        {
            if (argument.startsWith ("--out="))
            {
                const auto path = argument.substring (6);
                outputFile = juce::File::isAbsolutePath (path)
                                 ? juce::File (path)
                                 : juce::File::getCurrentWorkingDirectory().getChildFile (path);
            }
        }

        outputFile = outputFile.getFullPathName();
        outputFile.getParentDirectory().createDirectory();

        processor = std::make_unique<GLimiterAudioProcessor>();
        editor = std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditor());

        if (editor == nullptr)
        {
            std::cerr << "Could not create the plugin editor.\n";
            setApplicationReturnValue (1);
            quit();
            return;
        }

        editor->setSize (1180, 720);
        editor->addToDesktop (0);
        editor->setTopLeftPosition (30, 30);
        editor->setVisible (true);
        editor->toFront (true);

        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition ({ 3000.0f, 3000.0f });

        processor->setPlayConfigDetails (2, 2, 48000.0, 1024);
        processor->prepareToPlay (48000.0, 1024);
        testBuffer.setSize (2, 1024);
        startTimerHz (30);

        juce::Timer::callAfterDelay (1500, [this]
        {
            stopTimer();
            juce::Desktop::getInstance().getMainMouseSource().setScreenPosition ({ 3000.0f, 3000.0f });

            auto hideTooltips = [&] (auto&& self, juce::Component& component) -> void
            {
                for (auto* child : component.getChildren())
                {
                    if (dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                        child->setVisible (false);
                    else
                        self (self, *child);
                }
            };
            hideTooltips (hideTooltips, *editor);

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

            if (image.isValid())
            {
                outputFile.deleteFile();

                if (auto stream = std::unique_ptr<juce::FileOutputStream> (outputFile.createOutputStream()))
                {
                    juce::PNGImageFormat png;
                    const bool written = png.writeImageToStream (image, *stream);
                    stream->flush();

                    if (written)
                        std::cout << "Rendered " << outputFile.getFullPathName() << "\n";
                    else
                        std::cerr << "Failed to encode PNG.\n";
                }
                else
                {
                    std::cerr << "Could not open " << outputFile.getFullPathName() << "\n";
                }
            }
            else
            {
                std::cerr << "Editor snapshot was invalid.\n";
            }

            processor->releaseResources();
            editor->removeFromDesktop();
            editor.reset();
            processor.reset();
            quit();
        });
    }

private:
    void timerCallback() override
    {
        if (processor == nullptr)
            return;

        for (int i = 0; i < testBuffer.getNumSamples(); ++i)
        {
            const float sample = guitarSample (sampleCounter + i, 48000.0);

            for (int ch = 0; ch < testBuffer.getNumChannels(); ++ch)
                testBuffer.setSample (ch, i, ch == 0 ? sample : sample * 0.94f);
        }

        sampleCounter += testBuffer.getNumSamples();
        juce::MidiBuffer midi;
        processor->processBlock (testBuffer, midi);
    }

    std::unique_ptr<GLimiterAudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::File outputFile;
    juce::AudioBuffer<float> testBuffer;
    juce::int64 sampleCounter = 0;
};

START_JUCE_APPLICATION (GLimiterRenderApplication)
