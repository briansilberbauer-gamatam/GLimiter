#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace glimiter
{

/**
    A guitar-first lookahead limiter.

    Design notes
    ------------
    - Fixed 2 ms lookahead so the host latency never changes while the unit is
      running. Bypass routes the same delayed raw signal, keeping phase intact.
    - The detector runs through a 2nd-order thump filter (high-pass) and a small
      amount of the unfiltered signal. Palm mutes therefore stop ducking the
      whole part while real peaks are still caught.
    - The attack control sets how much of the 2 ms lookahead is used for the
      gain ramp (1 sample .. full window). The required gain is taken as the
      minimum across that window, so the delayed sample can never exceed the
      requested ceiling.
    - Release is programme dependent in AUTO mode: light gain reduction
      recovers quickly, deep limiting recovers slowly.
    - A final soft-ceiling safety stage makes the output asymptotic to the
      ceiling, so even hostile float input cannot produce an overs.
    - No allocation, locks, or non-realtime calls inside process().
*/
class GuitarLimiter
{
public:
    static constexpr double lookaheadMs = 2.0;
    static constexpr float maxLookaheadMs = 2.0f;

    struct Parameters
    {
        bool enabled = true;
        bool shapeEnabled = true;
        bool bypassed = false;

        float thresholdDb = -12.0f;
        float ceilingDb = -0.3f;
        float attackMs = 0.8f;
        float releaseMs = 140.0f;
        float kneeDb = 6.0f;
        float detectorHpfHz = 70.0f;
        bool autoRelease = true;

        float bodyDb = 0.0f;
        float airDb = 0.0f;
        float bite = 0.15f;
        float mix = 1.0f;
    };

    GuitarLimiter() = default;

    void prepare (double newSampleRate, int maxBlockSize, int numChannels)
    {
        sampleRate = std::max (8000.0, newSampleRate);
        preparedChannels = std::max (1, std::min (2, numChannels));

        lookaheadSamples = std::max (1, juce::roundToInt (sampleRate * lookaheadMs * 0.001));
        delaySize = lookaheadSamples + 1;

        rawDelay.resize (static_cast<size_t> (preparedChannels));
        shapedDelay.resize (static_cast<size_t> (preparedChannels));

        for (auto& channel : rawDelay)
            channel.assign (static_cast<size_t> (delaySize), 0.0f);

        for (auto& channel : shapedDelay)
            channel.assign (static_cast<size_t> (delaySize), 0.0f);

        requiredGainHistory.assign (static_cast<size_t> (delaySize), 1.0f);

        bodyFilter.assign (static_cast<size_t> (preparedChannels), OnePole {});
        airFilter.assign (static_cast<size_t> (preparedChannels), OnePole {});
        thumpFilter.assign (static_cast<size_t> (preparedChannels), TptHighPass {});

        // One-pole split points: 180 Hz body shelf, 3.2 kHz air shelf.
        bodyCoefficient = static_cast<float> (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 180.0 / sampleRate));
        airCoefficient  = static_cast<float> (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 3200.0 / sampleRate));

        smoothedBody.reset (sampleRate, 0.02);
        smoothedAir.reset (sampleRate, 0.02);
        smoothedBite.reset (sampleRate, 0.02);
        smoothedMix.reset (sampleRate, 0.02);

        smoothedBody.setCurrentAndTargetValue (1.0f);
        smoothedAir.setCurrentAndTargetValue (1.0f);
        smoothedBite.setCurrentAndTargetValue (0.0f);
        smoothedMix.setCurrentAndTargetValue (1.0f);

        juce::ignoreUnused (maxBlockSize);

        reset();
    }

    void reset()
    {
        for (auto& channel : rawDelay)
            std::fill (channel.begin(), channel.end(), 0.0f);

        for (auto& channel : shapedDelay)
            std::fill (channel.begin(), channel.end(), 0.0f);

        std::fill (requiredGainHistory.begin(), requiredGainHistory.end(), 1.0f);

        for (auto& filter : bodyFilter) filter.state = 0.0f;
        for (auto& filter : airFilter)  filter.state = 0.0f;
        for (auto& filter : thumpFilter) filter.reset();

        writeIndex = 0;
        sampleCounter = 0;
        currentGain = 1.0f;
        lastGainReductionDb.store (0.0f, std::memory_order_relaxed);
        detectorPeak.store (0.0f, std::memory_order_relaxed);
    }

    void process (juce::AudioBuffer<float>& buffer, const Parameters& parameters)
    {
        const int numChannels = std::min (preparedChannels, buffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0 || delaySize <= 0
            || rawDelay.size() < static_cast<size_t> (numChannels)
            || shapedDelay.size() < static_cast<size_t> (numChannels)
            || requiredGainHistory.size() < static_cast<size_t> (delaySize))
            return;

        const float thresholdDb = juce::jlimit (-48.0f, -0.5f, parameters.thresholdDb);
        const float ceilingDb = juce::jlimit (-24.0f, 0.0f, parameters.ceilingDb);
        const float ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
        const float kneeDb = juce::jlimit (0.0f, 18.0f, parameters.kneeDb);
        const float releaseMs = juce::jlimit (10.0f, 1200.0f, parameters.releaseMs);
        const bool limiting = parameters.enabled && ! parameters.bypassed;
        const bool shaping = parameters.shapeEnabled && ! parameters.bypassed;

        // Gain-computer slope: 0 dBFS input is mapped exactly to the ceiling.
        const float usableRange = std::max (0.5f, -thresholdDb);
        const float ceilingWeight = juce::jlimit (0.0f, 1.0f, (-ceilingDb) / usableRange);
        const float slope = juce::jlimit (0.0f, 0.999f, ceilingWeight);

        const int attackWindow = limiting
            ? juce::jlimit (1, lookaheadSamples + 1,
                            juce::roundToInt (juce::jlimit (0.02f, maxLookaheadMs, parameters.attackMs)
                                              * 0.001f * static_cast<float> (sampleRate)))
            : 1;

        const float lastReduction = lastGainReductionDb.load (std::memory_order_relaxed);
        float effectiveReleaseMs = releaseMs;

        if (parameters.autoRelease)
        {
            const float programme = juce::jlimit (0.0f, 1.0f, lastReduction / 18.0f);
            effectiveReleaseMs *= 0.55f + 0.75f * programme;
        }

        effectiveReleaseMs = juce::jlimit (8.0f, 1800.0f, effectiveReleaseMs);
        const float releaseCoefficient = static_cast<float> (
            1.0 - std::exp (-1.0 / (0.001 * static_cast<double> (effectiveReleaseMs) * sampleRate)));

        // Detector high-pass coefficients (Butterworth, TPT state-variable).
        const float cutoff = juce::jlimit (20.0f, 400.0f, parameters.detectorHpfHz);
        const float g = static_cast<float> (std::tan (juce::MathConstants<double>::pi
                                                       * static_cast<double> (cutoff) / sampleRate));
        constexpr float k = 1.41421356f;

        if (shaping)
        {
            smoothedBody.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (-12.0f, 12.0f, parameters.bodyDb)));
            smoothedAir.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (-12.0f, 12.0f, parameters.airDb)));
            smoothedBite.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.bite));
        }
        else
        {
            smoothedBody.setTargetValue (1.0f);
            smoothedAir.setTargetValue (1.0f);
            smoothedBite.setTargetValue (0.0f);
        }

        const float mix = parameters.bypassed ? 0.0f : juce::jlimit (0.0f, 1.0f, parameters.mix);
        smoothedMix.setTargetValue (mix);

        float blockReduction = 0.0f;
        float blockDetectorPeak = 0.0f;

        auto* const* channelData = buffer.getArrayOfWritePointers();

        for (int i = 0; i < numSamples; ++i)
        {
            const int readIndex = (writeIndex + 1) % delaySize;

            float delayedRaw[2] { 0.0f, 0.0f };
            float delayedShaped[2] { 0.0f, 0.0f };

            for (int ch = 0; ch < numChannels; ++ch)
            {
                delayedRaw[ch] = rawDelay[static_cast<size_t> (ch)][static_cast<size_t> (readIndex)];
                delayedShaped[ch] = shapedDelay[static_cast<size_t> (ch)][static_cast<size_t> (readIndex)];
            }

            const float bodyGain = smoothedBody.getNextValue();
            const float airGain = smoothedAir.getNextValue();
            const float bite = smoothedBite.getNextValue();
            const float currentMix = smoothedMix.getNextValue();

            float currentRaw[2] { 0.0f, 0.0f };
            float shaped[2] { 0.0f, 0.0f };
            float detector = 0.0f;
            float rawPeak = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float sample = channelData[ch][i];

                if (! std::isfinite (sample))
                    sample = 0.0f;

                currentRaw[ch] = sample;
                rawPeak = std::max (rawPeak, std::abs (sample));

                float value = sample;

                if (shaping)
                {
                    const float low = bodyFilter[static_cast<size_t> (ch)].process (value, bodyCoefficient);
                    value += (bodyGain - 1.0f) * low;

                    const float highLow = airFilter[static_cast<size_t> (ch)].process (value, airCoefficient);
                    value += (airGain - 1.0f) * (value - highLow);

                    if (bite > 1.0e-4f)
                    {
                        const float drive = 1.0f + 3.0f * bite;
                        const float wet = std::tanh (value * drive) / (0.75f * drive + 0.25f);
                        value += bite * (wet - value);
                    }
                }

                shaped[ch] = value;

                // Thump filtering for the sidechain only.
                const float highPassed = thumpFilter[static_cast<size_t> (ch)].process (value, g, k);
                detector = std::max (detector, std::abs (highPassed));
            }

            // Keep a little full-band information in the detector so very low
            // notes cannot slip past the thump filter entirely.
            detector = std::max (detector, rawPeak * 0.35f);
            blockDetectorPeak = std::max (blockDetectorPeak, detector);

            const float requiredGain = limiting ? requiredGainForPeak (detector, thresholdDb,
                                                                       ceilingDb, kneeDb, slope)
                                                : 1.0f;

            requiredGainHistory[static_cast<size_t> (writeIndex)] = requiredGain;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                rawDelay[static_cast<size_t> (ch)][static_cast<size_t> (writeIndex)] = currentRaw[ch];
                shapedDelay[static_cast<size_t> (ch)][static_cast<size_t> (writeIndex)] = shaped[ch];
            }

            // Hold the output for the first lookahead samples so the delay is
            // primed before the limiter starts applying gain.
            if (sampleCounter >= lookaheadSamples)
            {
                const int outputIndex = (writeIndex + 1) % delaySize;
                float minimumGain = 1.0f;

                if (limiting)
                {
                    for (int w = 0; w < attackWindow; ++w)
                    {
                        const int index = (outputIndex + w) % delaySize;
                        minimumGain = std::min (minimumGain,
                                                requiredGainHistory[static_cast<size_t> (index)]);
                    }

                    if (minimumGain < currentGain)
                        currentGain = minimumGain;
                    else
                        currentGain += releaseCoefficient * (minimumGain - currentGain);

                    currentGain = juce::jlimit (0.0f, 1.0f, currentGain);
                    blockReduction = std::max (blockReduction,
                                               -juce::Decibels::gainToDecibels (std::max (currentGain, 1.0e-6f), -120.0f));
                }
                else
                {
                    currentGain += releaseCoefficient * (1.0f - currentGain);

                    if (currentGain > 0.99999f)
                        currentGain = 1.0f;
                }

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float dry = delayedShaped[ch];
                    const float wet = limiting ? softCeiling (dry * currentGain, ceilingLinear) : dry * currentGain;
                    const float output = parameters.bypassed
                                             ? delayedRaw[ch]
                                             : dry + currentMix * (wet - dry);

                    channelData[ch][i] = std::isfinite (output) ? output : 0.0f;
                }
            }
            else
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    channelData[ch][i] = 0.0f;
            }

            writeIndex = (writeIndex + 1) % delaySize;
            ++sampleCounter;
        }

        lastGainReductionDb.store (blockReduction, std::memory_order_relaxed);
        detectorPeak.store (blockDetectorPeak, std::memory_order_relaxed);
    }

    int getLatencySamples() const noexcept { return lookaheadSamples; }
    float getGainReductionDb() const noexcept { return lastGainReductionDb.load (std::memory_order_relaxed); }
    float getDetectorPeak() const noexcept { return detectorPeak.load (std::memory_order_relaxed); }

private:
    struct OnePole
    {
        float state = 0.0f;

        float process (float input, float coefficient) noexcept
        {
            state += coefficient * (input - state);
            return state;
        }
    };

    struct TptHighPass
    {
        float ic1 = 0.0f;
        float ic2 = 0.0f;

        void reset() noexcept
        {
            ic1 = ic2 = 0.0f;
        }

        float process (float input, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            const float v3 = input - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;

            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;

            return input - k * v1 - v2;
        }
    };

    static float requiredGainForPeak (float peak, float thresholdDb, float ceilingDb,
                                      float kneeDb, float slope) noexcept
    {
        if (peak <= 1.0e-8f)
            return 1.0f;

        const float peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
        const float over = peakDb - thresholdDb;

        float reductionDb = 0.0f;

        if (kneeDb > 0.001f)
        {
            if (over >= kneeDb * 0.5f)
                reductionDb = -slope * over;
            else if (over > -kneeDb * 0.5f)
            {
                const float position = over + kneeDb * 0.5f;
                reductionDb = -slope * position * position / (2.0f * kneeDb);
            }
        }
        else if (over > 0.0f)
        {
            reductionDb = -slope * over;
        }

        // Absolute ceiling guarantee for peaks above 0 dBFS or a fast transient
        // that outran the knee.
        reductionDb = std::min (reductionDb, ceilingDb - peakDb);
        reductionDb = juce::jlimit (-72.0f, 0.0f, reductionDb);

        return juce::Decibels::decibelsToGain (reductionDb);
    }

    static float softCeiling (float sample, float ceiling) noexcept
    {
        if (ceiling <= 0.0f)
            return 0.0f;

        const float magnitude = std::abs (sample);
        const float knee = ceiling * 0.90f;

        if (magnitude <= knee)
            return sample;

        const float range = juce::jmax (1.0e-6f, ceiling - knee);
        const float limited = knee + range * std::tanh ((magnitude - knee) / range);

        return std::copysign (juce::jmin (limited, ceiling), sample);
    }

    double sampleRate = 48000.0;
    int preparedChannels = 2;
    int lookaheadSamples = 96;
    int delaySize = 97;
    int writeIndex = 0;
    int64_t sampleCounter = 0;

    std::vector<std::vector<float>> rawDelay;
    std::vector<std::vector<float>> shapedDelay;
    std::vector<float> requiredGainHistory;

    std::vector<OnePole> bodyFilter;
    std::vector<OnePole> airFilter;
    std::vector<TptHighPass> thumpFilter;

    float bodyCoefficient = 0.0f;
    float airCoefficient = 0.0f;

    juce::SmoothedValue<float> smoothedBody { 1.0f };
    juce::SmoothedValue<float> smoothedAir { 1.0f };
    juce::SmoothedValue<float> smoothedBite { 0.0f };
    juce::SmoothedValue<float> smoothedMix { 1.0f };

    float currentGain = 1.0f;
    std::atomic<float> lastGainReductionDb { 0.0f };
    std::atomic<float> detectorPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarLimiter)
};

} // namespace glimiter
