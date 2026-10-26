#include "dsp/GuitarLimiter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const juce::String& message)
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << message << '\n';

    if (! condition)
        ++failures;
}

constexpr double sampleRate = 48000.0;

glimiter::GuitarLimiter::Parameters baseParameters()
{
    glimiter::GuitarLimiter::Parameters p;
    p.enabled = true;
    p.shapeEnabled = false;
    p.bypassed = false;
    p.thresholdDb = -12.0f;
    p.ceilingDb = -0.3f;
    p.attackMs = 0.8f;
    p.releaseMs = 140.0f;
    p.kneeDb = 6.0f;
    p.detectorHpfHz = 70.0f;
    p.autoRelease = false;
    p.bodyDb = 0.0f;
    p.airDb = 0.0f;
    p.bite = 0.0f;
    p.mix = 1.0f;
    return p;
}

juce::AudioBuffer<float> makeSine (float amplitude, double frequency, int numSamples,
                                   int numChannels = 2)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;
            data[i] = amplitude * static_cast<float> (
                std::sin (juce::MathConstants<double>::twoPi * frequency * t));
        }
    }

    return buffer;
}

float peakAfterLatency (const juce::AudioBuffer<float>& buffer, int latency, int channel = 0)
{
    const int start = juce::jmin (buffer.getNumSamples() - 1, latency + 256);
    return buffer.getMagnitude (channel, start, buffer.getNumSamples() - start);
}

float rmsAfterLatency (const juce::AudioBuffer<float>& buffer, int latency, int channel = 0)
{
    const int start = juce::jmin (buffer.getNumSamples() - 1, latency + 256);
    double sum = 0.0;
    const auto* data = buffer.getReadPointer (channel);

    for (int i = start; i < buffer.getNumSamples(); ++i)
        sum += static_cast<double> (data[i]) * static_cast<double> (data[i]);

    return static_cast<float> (std::sqrt (sum / static_cast<double> (buffer.getNumSamples() - start)));
}

juce::AudioBuffer<float> render (glimiter::GuitarLimiter& limiter,
                                 const glimiter::GuitarLimiter::Parameters& params,
                                 juce::AudioBuffer<float> input,
                                 int numChannels = 2)
{
    limiter.prepare (sampleRate, 512, numChannels);
    limiter.process (input, params);
    return input;
}

void testLatency()
{
    glimiter::GuitarLimiter limiter;
    limiter.prepare (sampleRate, 512, 2);
    check (limiter.getLatencySamples() == 96, "48 kHz reports exactly 2 ms (96 samples) of lookahead");

    limiter.prepare (96000.0, 512, 2);
    check (limiter.getLatencySamples() == 192, "96 kHz reports exactly 2 ms (192 samples) of lookahead");
}

void testTransparentBelowThreshold()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();
    params.shapeEnabled = true;
    params.bodyDb = 0.0f;
    params.airDb = 0.0f;
    params.bite = 0.0f;

    auto output = render (limiter, params, makeSine (0.08f, 220.0, 24000), 2);
    const float peak = peakAfterLatency (output, limiter.getLatencySamples());
    check (std::abs (peak - 0.08f) < 0.002f, "signal below threshold passes at unity gain");
    check (limiter.getGainReductionDb() < 0.05f, "no gain reduction below threshold");
}

void testBrickwallCeiling()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();
    params.thresholdDb = -12.0f;
    params.ceilingDb = -1.0f;
    params.autoRelease = true;

    auto output = render (limiter, params, makeSine (1.0f, 440.0, 48000), 2);
    const float ceiling = juce::Decibels::decibelsToGain (-1.0f);
    const float peak = peakAfterLatency (output, limiter.getLatencySamples());

    check (peak <= ceiling + 1.0e-4f, "brickwall ceiling is respected for a 0 dBFS sine");
    check (limiter.getGainReductionDb() > 0.5f, "hot signal produces meaningful gain reduction");
}

void testExtremeInputCeiling()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();
    params.ceilingDb = -0.3f;

    auto output = render (limiter, params, makeSine (8.0f, 220.0, 24000), 2);
    const float ceiling = juce::Decibels::decibelsToGain (-0.3f);
    const float peak = peakAfterLatency (output, limiter.getLatencySamples());

    check (std::isfinite (peak), "extreme input remains finite");
    check (peak <= ceiling + 1.0e-4f, "safety ceiling holds for +18 dBFS input");
}

void testBypassIsDelayedRaw()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();
    params.bypassed = true;
    params.mix = 1.0f;

    juce::AudioBuffer<float> input (2, 4096);
    input.clear();
    input.setSample (0, 100, 0.75f);
    input.setSample (1, 100, -0.5f);

    auto output = render (limiter, params, input, 2);
    const int latency = limiter.getLatencySamples();

    check (std::abs (output.getSample (0, 100 + latency) - 0.75f) < 1.0e-6f,
           "bypass left channel is the raw input delayed by the reported latency");
    check (std::abs (output.getSample (1, 100 + latency) + 0.5f) < 1.0e-6f,
           "bypass right channel is the raw input delayed by the reported latency");
    check (output.getMagnitude (0, 0, 4096) <= 0.75f + 1.0e-6f, "bypass does not generate extra peaks");
}

void testParallelMixZero()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();
    params.mix = 0.0f;
    params.thresholdDb = -24.0f;

    auto input = makeSine (0.9f, 330.0, 24000);
    auto output = render (limiter, params, input, 2);
    const int latency = limiter.getLatencySamples();

    float maximumDifference = 0.0f;

    for (int i = latency + 4800; i < input.getNumSamples(); ++i)
        maximumDifference = std::max (maximumDifference,
                                      std::abs (output.getSample (0, i) - input.getSample (0, i - latency)));

    check (maximumDifference < 1.0e-5f, "100% dry mix reproduces the latency-aligned input");
}

void testLimiterDisabled()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();
    params.enabled = false;

    auto output = render (limiter, params, makeSine (0.7f, 330.0, 24000), 2);
    const float peak = peakAfterLatency (output, limiter.getLatencySamples());

    check (std::abs (peak - 0.7f) < 0.01f, "limiter disable leaves the signal untouched");
    check (limiter.getGainReductionDb() < 0.05f, "limiter disable reports no gain reduction");
}

void testThumpFilter()
{
    auto measure = [] (float cutoff)
    {
        glimiter::GuitarLimiter limiter;
        auto params = baseParameters();
        params.thresholdDb = -18.0f;
        params.ceilingDb = -0.5f;
        params.detectorHpfHz = cutoff;
        auto output = render (limiter, params, makeSine (1.5f, 55.0, 24000), 2);
        juce::ignoreUnused (output);
        return limiter.getGainReductionDb();
    };

    const float lowCutoffReduction = measure (20.0f);
    const float highCutoffReduction = measure (300.0f);

    check (lowCutoffReduction > highCutoffReduction + 2.0f,
           "high thump-filter cutoff reduces low-frequency detector pumping");
}

void testBodyShelf()
{
    auto measure = [] (float bodyDb)
    {
        glimiter::GuitarLimiter limiter;
        auto params = baseParameters();
        params.shapeEnabled = true;
        params.thresholdDb = -3.0f;
        params.ceilingDb = 0.0f;
        params.bodyDb = bodyDb;
        params.airDb = 0.0f;
        params.bite = 0.0f;

        auto output = render (limiter, params, makeSine (0.12f, 80.0, 24000), 2);
        return peakAfterLatency (output, limiter.getLatencySamples());
    };

    check (measure (9.0f) > measure (-9.0f) * 2.5f, "BODY shelf boosts low guitar frequencies");
}

void testAirShelf()
{
    auto measure = [] (float airDb)
    {
        glimiter::GuitarLimiter limiter;
        auto params = baseParameters();
        params.shapeEnabled = true;
        params.thresholdDb = -3.0f;
        params.ceilingDb = 0.0f;
        params.bodyDb = 0.0f;
        params.airDb = airDb;
        params.bite = 0.0f;

        auto output = render (limiter, params, makeSine (0.12f, 5000.0, 24000), 2);
        return peakAfterLatency (output, limiter.getLatencySamples());
    };

    check (measure (9.0f) > measure (-9.0f) * 1.7f, "AIR shelf boosts guitar pick brightness");
}

void testBiteSaturation()
{
    auto crestFactor = [] (float bite)
    {
        glimiter::GuitarLimiter limiter;
        auto params = baseParameters();
        params.shapeEnabled = true;
        params.thresholdDb = -3.0f;
        params.ceilingDb = 0.0f;
        params.bodyDb = 0.0f;
        params.airDb = 0.0f;
        params.bite = bite;

        limiter.prepare (sampleRate, 512, 2);

        // Let the 20 ms parameter smoother settle before measuring steady state.
        auto warmUp = makeSine (0.5f, 220.0, 4800, 2);
        limiter.process (warmUp, params);

        auto output = makeSine (0.5f, 220.0, 24000, 2);
        limiter.process (output, params);

        const int latency = limiter.getLatencySamples();
        return peakAfterLatency (output, latency) / juce::jmax (1.0e-6f, rmsAfterLatency (output, latency));
    };

    check (crestFactor (1.0f) < crestFactor (0.0f) - 0.03f,
           "BITE saturation compresses the waveform crest factor");
}

void testMonoAndSilence()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();

    auto mono = render (limiter, params, makeSine (0.6f, 196.0, 12000, 1), 1);
    check (std::isfinite (peakAfterLatency (mono, limiter.getLatencySamples())),
           "mono processing stays finite");

    juce::AudioBuffer<float> silence (2, 8192);
    silence.clear();
    auto output = render (limiter, params, silence, 2);
    check (output.getMagnitude (0, 8192) < 1.0e-8f, "silence remains silent");
}

void testAutoReleaseRecovers()
{
    glimiter::GuitarLimiter limiter;
    auto params = baseParameters();
    params.thresholdDb = -24.0f;
    params.ceilingDb = -0.5f;
    params.autoRelease = true;
    params.releaseMs = 250.0f;

    auto buffer = makeSine (1.0f, 220.0, 48000, 2);

    // Drop the level halfway through so the envelope has to release.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 24000; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * 0.02f);

    limiter.prepare (sampleRate, 512, 2);

    for (int offset = 0; offset + 512 <= buffer.getNumSamples(); offset += 512)
    {
        float* channels[2] { buffer.getWritePointer (0) + offset,
                             buffer.getWritePointer (1) + offset };
        juce::AudioBuffer<float> block (channels, 2, 512);
        limiter.process (block, params);
    }

    const float finalPeak = buffer.getMagnitude (0, 45000, 3000);

    check (limiter.getGainReductionDb() < 1.0f, "auto release recovers after the signal drops");
    check (std::isfinite (finalPeak), "release stage remains finite");
}

} // namespace

int main()
{
    std::cout << "GLimiter DSP tests\n"
              << "==================\n";

    testLatency();
    testTransparentBelowThreshold();
    testBrickwallCeiling();
    testExtremeInputCeiling();
    testBypassIsDelayedRaw();
    testParallelMixZero();
    testLimiterDisabled();
    testThumpFilter();
    testBodyShelf();
    testAirShelf();
    testBiteSaturation();
    testMonoAndSilence();
    testAutoReleaseRecovers();

    std::cout << '\n' << (failures == 0 ? "All DSP tests passed.\n"
                                        : "DSP tests failed: " + std::to_string (failures) + "\n");

    return failures == 0 ? 0 : 1;
}
