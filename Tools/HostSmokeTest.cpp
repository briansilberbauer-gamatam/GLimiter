#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <iostream>

namespace
{
int failures = 0;

void check (bool condition, const char* message)
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << message << '\n';

    if (! condition)
        ++failures;
}

void fillSine (juce::AudioBuffer<float>& buffer, int offset, float amplitude)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto n = static_cast<double> (offset + i);
            buffer.setSample (ch, i, amplitude * static_cast<float> (
                std::sin (juce::MathConstants<double>::twoPi * 196.0 * n / 48000.0)));
        }
    }
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getReadPointer (ch)[i]))
                return false;

    return true;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc < 2)
    {
        std::cerr << "Usage: GLimiterHostSmokeTest /path/to/GLimiter.vst3\n";
        return 2;
    }

    const juce::File pluginFile { juce::String (argv[1]) };

    if (! pluginFile.exists())
    {
        std::cerr << "Plugin bundle not found: " << pluginFile.getFullPathName() << '\n';
        return 2;
    }

    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    juce::OwnedArray<juce::PluginDescription> descriptions;

    for (auto* format : formatManager.getFormats())
        if (auto* vst3 = dynamic_cast<juce::VST3PluginFormat*> (format))
            vst3->findAllTypesForFile (descriptions, pluginFile.getFullPathName());

    check (! descriptions.isEmpty(), "VST3 scanner discovers the plugin");

    if (descriptions.isEmpty())
        return 1;

    auto& description = *descriptions[0];

    std::cout << "  INFO  " << description.name << " / " << description.manufacturerName
              << " / " << description.version << '\n';

    juce::String error;
    auto instance = formatManager.createPluginInstance (description, 48000.0, 512, error);

    check (instance != nullptr, "plugin instance can be created");

    if (instance == nullptr)
    {
        std::cerr << "  ERROR " << error << '\n';
        return 1;
    }

    check (instance->getParameters().size() >= 15, "all guitar-limiter parameters are exposed to the host");
    check (instance->getNumPrograms() == 8, "eight factory programmes are exposed to the host");
    check (instance->getProgramName (0) == "Rhythm Glue", "the default programme is Rhythm Glue");

    instance->setPlayConfigDetails (2, 2, 48000.0, 512);
    instance->prepareToPlay (48000.0, 512);

    check (instance->getLatencySamples() == 96, "host sees the fixed 2 ms lookahead latency");

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    bool finite = true;
    float outputPeak = 0.0f;

    for (int block = 0; block < 60; ++block)
    {
        fillSine (buffer, block * 512, 1.0f);
        instance->processBlock (buffer, midi);
        finite = finite && allFinite (buffer);
        outputPeak = std::max (outputPeak, buffer.getMagnitude (0, 0, buffer.getNumSamples()));
    }

    check (finite, "audio thread produces finite output through the VST3 wrapper");
    check (outputPeak <= 0.98f, "hot sine respects the default -0.3 dBFS ceiling");
    check (outputPeak > 0.1f, "hot sine is audible after limiting");

    bool presetsFinite = true;

    for (int program = 0; program < instance->getNumPrograms(); ++program)
    {
        instance->setCurrentProgram (program);

        for (int block = 0; block < 12; ++block)
        {
            fillSine (buffer, block * 512, 1.4f);
            instance->processBlock (buffer, midi);
            presetsFinite = presetsFinite && allFinite (buffer);
        }
    }

    check (presetsFinite, "every factory programme processes audio safely");
    instance->setCurrentProgram (0);

    // Parameter state round trip.
    auto parameters = instance->getParameters();
    auto* firstParameter = parameters[0];
    firstParameter->setValueNotifyingHost (0.73f);
    const float expected = firstParameter->getValue();

    juce::MemoryBlock state;
    instance->getStateInformation (state);
    check (state.getSize() > 0, "state can be serialised for the host");

    firstParameter->setValueNotifyingHost (0.11f);
    instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (std::abs (firstParameter->getValue() - expected) < 1.0e-4f,
           "parameter values survive a state round trip");

    instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (true, "serialised state can be restored");

    instance->releaseResources();

    std::cout << (failures == 0 ? "\nHost smoke test passed.\n" : "\nHost smoke test failed.\n");
    return failures == 0 ? 0 : 1;
}
