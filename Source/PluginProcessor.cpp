#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <algorithm>
#include <cmath>

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id,
                                                      const char* name,
                                                      juce::NormalisableRange<float> range,
                                                      float defaultValue,
                                                      const char* label)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 },
        name,
        range,
        defaultValue,
        juce::AudioParameterFloatAttributes().withLabel (label));
}

std::unique_ptr<juce::AudioParameterBool> makeBool (const char* id,
                                                    const char* name,
                                                    bool defaultValue)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
}
} // namespace

GLimiterAudioProcessor::GLimiterAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inputGainParam = apvts.getRawParameterValue (glimiter::params::inputGain);
    outputGainParam = apvts.getRawParameterValue (glimiter::params::outputGain);
    mixParam = apvts.getRawParameterValue (glimiter::params::mix);
    bypassParam = apvts.getRawParameterValue (glimiter::params::bypass);

    limiterOnParam = apvts.getRawParameterValue (glimiter::params::limiterOn);
    thresholdParam = apvts.getRawParameterValue (glimiter::params::threshold);
    ceilingParam = apvts.getRawParameterValue (glimiter::params::ceiling);
    attackParam = apvts.getRawParameterValue (glimiter::params::attack);
    releaseParam = apvts.getRawParameterValue (glimiter::params::release);
    kneeParam = apvts.getRawParameterValue (glimiter::params::knee);
    detectorHpfParam = apvts.getRawParameterValue (glimiter::params::detectorHpf);
    autoReleaseParam = apvts.getRawParameterValue (glimiter::params::autoRelease);

    shapeOnParam = apvts.getRawParameterValue (glimiter::params::shapeOn);
    bodyParam = apvts.getRawParameterValue (glimiter::params::body);
    airParam = apvts.getRawParameterValue (glimiter::params::air);
    biteParam = apvts.getRawParameterValue (glimiter::params::bite);

    for (const auto* parameterID : glimiter::params::allParameterIDs)
        apvts.addParameterListener (parameterID, this);

    for (auto& sample : waveformRing)
        sample.store (0.0f, std::memory_order_relaxed);

    currentPreset.store (0, std::memory_order_relaxed);
    presetDirty.store (false, std::memory_order_relaxed);
}

GLimiterAudioProcessor::~GLimiterAudioProcessor()
{
    for (const auto* parameterID : glimiter::params::allParameterIDs)
        apvts.removeParameterListener (parameterID, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout GLimiterAudioProcessor::createParameterLayout()
{
    using namespace glimiter::params;

    APVTS::ParameterLayout layout;

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", " | ");
    global->addChild (makeFloat (inputGain, "Input", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB"));
    global->addChild (makeFloat (outputGain, "Output", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB"));
    global->addChild (makeFloat (mix, "Mix", { 0.0f, 1.0f, 0.001f }, 1.0f, "%"));
    global->addChild (makeBool (bypass, "Bypass", false));
    layout.add (std::move (global));

    auto limiterGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("limiter", "Limiter", " | ");
    limiterGroup->addChild (makeBool (limiterOn, "Limiter On", true));
    limiterGroup->addChild (makeFloat (threshold, "Threshold", { -48.0f, -0.5f, 0.01f }, -12.0f, "dB"));
    limiterGroup->addChild (makeFloat (ceiling, "Ceiling", { -24.0f, 0.0f, 0.01f }, -0.3f, "dB"));

    // Attack only needs to describe the 2 ms lookahead ramp; keeping it short
    // is what preserves the pick transient.
    juce::NormalisableRange<float> attackRange { 0.05f, 2.0f, 0.01f };
    attackRange.setSkewForCentre (0.6f);
    limiterGroup->addChild (makeFloat (attack, "Attack", attackRange, 0.8f, "ms"));

    juce::NormalisableRange<float> releaseRange { 10.0f, 1200.0f, 1.0f };
    releaseRange.setSkewForCentre (160.0f);
    limiterGroup->addChild (makeFloat (release, "Release", releaseRange, 140.0f, "ms"));

    limiterGroup->addChild (makeFloat (knee, "Knee", { 0.0f, 18.0f, 0.01f }, 6.0f, "dB"));

    juce::NormalisableRange<float> thumpRange { 20.0f, 400.0f, 1.0f };
    thumpRange.setSkewForCentre (100.0f);
    limiterGroup->addChild (makeFloat (detectorHpf, "Thump Filter", thumpRange, 70.0f, "Hz"));

    limiterGroup->addChild (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { autoRelease, 1 },
        "Release Mode",
        juce::StringArray { "MANUAL", "AUTO" },
        1));
    layout.add (std::move (limiterGroup));

    auto shape = std::make_unique<juce::AudioProcessorParameterGroup> ("guitar", "Guitar", " | ");
    shape->addChild (makeBool (shapeOn, "Shape On", true));
    shape->addChild (makeFloat (body, "Body", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB"));
    shape->addChild (makeFloat (air, "Air", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB"));
    shape->addChild (makeFloat (bite, "Bite", { 0.0f, 1.0f, 0.001f }, 0.15f, "%"));
    layout.add (std::move (shape));

    return layout;
}

void GLimiterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = std::max (8000.0, sampleRate);
    preparedChannels = juce::jlimit (1, 2, juce::jmax (1, juce::jmax (getTotalNumInputChannels(),
                                                                      getTotalNumOutputChannels())));

    limiter.prepare (currentSampleRate, std::max (1, samplesPerBlock), preparedChannels);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (std::max (1, samplesPerBlock));
    spec.numChannels = static_cast<juce::uint32> (preparedChannels);

    inputGainDsp.prepare (spec);
    outputGainDsp.prepare (spec);
    inputGainDsp.setRampDurationSeconds (0.02);
    outputGainDsp.setRampDurationSeconds (0.02);
    inputGainDsp.setGainLinear (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGainDsp.setGainLinear (juce::Decibels::decibelsToGain (outputGainParam->load()));

    // Fixed 2 ms lookahead, identical whether the unit is active or bypassed.
    setLatencySamples (limiter.getLatencySamples());

    inputLevel.store (0.0f, std::memory_order_relaxed);
    outputLevel.store (0.0f, std::memory_order_relaxed);
    outputLevelLeft.store (0.0f, std::memory_order_relaxed);
    outputLevelRight.store (0.0f, std::memory_order_relaxed);

    for (auto& sample : waveformRing)
        sample.store (0.0f, std::memory_order_relaxed);

    waveformWriteIndex.store (0, std::memory_order_relaxed);
}

void GLimiterAudioProcessor::releaseResources()
{
    limiter.reset();
    inputGainDsp.reset();
    outputGainDsp.reset();
}

void GLimiterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    for (int ch = preparedChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0)
        return;

    const int processChannels = std::min (preparedChannels, buffer.getNumChannels());

    if (processChannels <= 0)
        return;

    juce::AudioBuffer<float> active (buffer.getArrayOfWritePointers(), processChannels, numSamples);

    const float inputPeak = active.getMagnitude (0, numSamples);
    const bool bypassed = bypassParam->load() > 0.5f;

    if (! bypassed)
    {
        juce::dsp::AudioBlock<float> block (active);
        juce::dsp::ProcessContextReplacing<float> context (block);

        inputGainDsp.setGainLinear (juce::Decibels::decibelsToGain (inputGainParam->load()));
        inputGainDsp.process (context);

        glimiter::GuitarLimiter::Parameters limiterParameters;
        limiterParameters.enabled = limiterOnParam->load() > 0.5f;
        limiterParameters.shapeEnabled = shapeOnParam->load() > 0.5f;
        limiterParameters.bypassed = false;
        limiterParameters.thresholdDb = thresholdParam->load();
        limiterParameters.ceilingDb = ceilingParam->load();
        limiterParameters.attackMs = attackParam->load();
        limiterParameters.releaseMs = releaseParam->load();
        limiterParameters.kneeDb = kneeParam->load();
        limiterParameters.detectorHpfHz = detectorHpfParam->load();
        limiterParameters.autoRelease = autoReleaseParam->load() > 0.5f;
        limiterParameters.bodyDb = bodyParam->load();
        limiterParameters.airDb = airParam->load();
        limiterParameters.bite = biteParam->load();
        limiterParameters.mix = mixParam->load();

        limiter.process (active, limiterParameters);

        outputGainDsp.setGainLinear (juce::Decibels::decibelsToGain (outputGainParam->load()));
        outputGainDsp.process (context);
    }
    else
    {
        glimiter::GuitarLimiter::Parameters limiterParameters;
        limiterParameters.bypassed = true;
        limiter.process (active, limiterParameters);
    }

    // Decimated post-processing mono waveform into the lock-free telemetry ring.
    {
        uint32_t writeIndex = waveformWriteIndex.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; i += waveformDecimation)
        {
            const float left  = active.getSample (0, i);
            const float right = processChannels > 1 ? active.getSample (1, i) : left;
            const float mono  = 0.5f * (left + right);

            waveformRing[writeIndex].store (std::isfinite (mono) ? mono : 0.0f,
                                            std::memory_order_relaxed);
            writeIndex = (writeIndex + 1) & static_cast<uint32_t> (waveformSize - 1);
        }

        waveformWriteIndex.store (writeIndex, std::memory_order_relaxed);
    }

    const float outputPeak = active.getMagnitude (0, numSamples);
    const float outputLeft = active.getMagnitude (0, 0, numSamples);
    const float outputRight = processChannels > 1 ? active.getMagnitude (1, 0, numSamples) : outputLeft;
    inputLevel.store (std::isfinite (inputPeak) ? inputPeak : 0.0f, std::memory_order_relaxed);
    outputLevel.store (std::isfinite (outputPeak) ? outputPeak : 0.0f, std::memory_order_relaxed);
    outputLevelLeft.store (std::isfinite (outputLeft) ? outputLeft : 0.0f, std::memory_order_relaxed);
    outputLevelRight.store (std::isfinite (outputRight) ? outputRight : 0.0f, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* GLimiterAudioProcessor::createEditor()
{
    return new GLimiterAudioProcessorEditor (*this);
}

int GLimiterAudioProcessor::getNumPrograms()
{
    return glimiter::getNumFactoryPresets();
}

int GLimiterAudioProcessor::getCurrentProgram()
{
    return currentPreset.load (std::memory_order_relaxed);
}

void GLimiterAudioProcessor::setCurrentProgram (int index)
{
    applyPreset (index);
}

const juce::String GLimiterAudioProcessor::getProgramName (int index)
{
    return glimiter::getFactoryPresetName (index);
}

void GLimiterAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void GLimiterAudioProcessor::applyPreset (int index)
{
    const auto& presets = glimiter::getFactoryPresets();

    if (index < 0 || index >= static_cast<int> (presets.size()))
        return;

    applyingPreset.store (true, std::memory_order_relaxed);

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& value : presets[static_cast<size_t> (index)].overrides)
    {
        if (auto* parameter = apvts.getParameter (value.id))
        {
            const auto& range = parameter->getNormalisableRange();
            const float deNormalised = juce::jlimit (range.start, range.end, value.value);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (deNormalised));
        }
    }

    currentPreset.store (index, std::memory_order_relaxed);
    presetDirty.store (false, std::memory_order_relaxed);
    applyingPreset.store (false, std::memory_order_relaxed);
}

void GLimiterAudioProcessor::parameterChanged (const juce::String&, float)
{
    if (! applyingPreset.load (std::memory_order_relaxed))
        presetDirty.store (true, std::memory_order_relaxed);
}

void GLimiterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetIndex", currentPreset.load (std::memory_order_relaxed), nullptr);
    state.setProperty ("presetDirty", presetDirty.load (std::memory_order_relaxed), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GLimiterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return;

    const int presetIndex = static_cast<int> (tree.getProperty ("presetIndex", 0));
    const bool dirty = static_cast<bool> (tree.getProperty ("presetDirty", false));

    applyingPreset.store (true, std::memory_order_relaxed);
    apvts.replaceState (tree);
    applyingPreset.store (false, std::memory_order_relaxed);

    currentPreset.store (juce::jlimit (0, glimiter::getNumFactoryPresets() - 1, presetIndex),
                         std::memory_order_relaxed);
    presetDirty.store (dirty, std::memory_order_relaxed);
}

bool GLimiterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainIn == juce::AudioChannelSet::mono()
        || mainIn == juce::AudioChannelSet::stereo();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GLimiterAudioProcessor();
}
