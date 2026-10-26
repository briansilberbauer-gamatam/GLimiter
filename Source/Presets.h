#pragma once

#include "Parameters.h"

#include <string>
#include <vector>

namespace glimiter
{

struct PresetValue
{
    const char* id;
    float value;
};

struct FactoryPreset
{
    const char* name;
    std::vector<PresetValue> overrides;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets
    {
        {
            // Default programme: measured limiting that keeps the pick audible.
            "Rhythm Glue",
            {
                { params::threshold, -12.0f }, { params::ceiling, -0.3f },
                { params::attack, 0.8f }, { params::release, 140.0f },
                { params::knee, 6.0f }, { params::detectorHpf, 70.0f },
                { params::autoRelease, 1.0f },
                { params::body, 0.0f }, { params::air, 0.0f },
                { params::bite, 0.15f }, { params::mix, 1.0f }
            }
        },
        {
            // Fast recovery and a raised thump filter keep palm mutes from ducking.
            "Palm-Mute Tamer",
            {
                { params::threshold, -18.0f }, { params::ceiling, -1.0f },
                { params::attack, 1.6f }, { params::release, 90.0f },
                { params::knee, 8.0f }, { params::detectorHpf, 160.0f },
                { params::autoRelease, 1.0f },
                { params::body, 2.0f }, { params::air, -1.0f },
                { params::bite, 0.5f }, { params::mix, 1.0f }
            }
        },
        {
            // Direct-injection sparkle: transparent, quick, bright.
            "DI Sparkle",
            {
                { params::threshold, -9.0f }, { params::ceiling, -0.5f },
                { params::attack, 0.3f }, { params::release, 220.0f },
                { params::knee, 4.0f }, { params::detectorHpf, 45.0f },
                { params::autoRelease, 0.0f },
                { params::body, -2.0f }, { params::air, 3.5f },
                { params::bite, 0.0f }, { params::mix, 1.0f }
            }
        },
        {
            // Long, vocal sustain for legato lead work.
            "Lead Sustain",
            {
                { params::threshold, -22.0f }, { params::ceiling, -0.8f },
                { params::attack, 1.8f }, { params::release, 320.0f },
                { params::knee, 10.0f }, { params::detectorHpf, 90.0f },
                { params::autoRelease, 1.0f },
                { params::body, 1.0f }, { params::air, 1.5f },
                { params::bite, 0.35f }, { params::mix, 0.90f }
            }
        },
        {
            // Open, airy limiting for acoustic pick work.
            "Acoustic Bloom",
            {
                { params::threshold, -8.0f }, { params::ceiling, -0.6f },
                { params::attack, 0.4f }, { params::release, 180.0f },
                { params::knee, 3.0f }, { params::detectorHpf, 55.0f },
                { params::autoRelease, 1.0f },
                { params::body, 1.0f }, { params::air, 2.0f },
                { params::bite, 0.1f }, { params::mix, 1.0f }
            }
        },
        {
            // Gritty front end feeding the brickwall.
            "Fuzz Wrangler",
            {
                { params::threshold, -16.0f }, { params::ceiling, -1.2f },
                { params::attack, 0.6f }, { params::release, 110.0f },
                { params::knee, 7.0f }, { params::detectorHpf, 120.0f },
                { params::autoRelease, 1.0f },
                { params::body, 1.0f }, { params::air, -1.0f },
                { params::bite, 0.85f }, { params::mix, 1.0f }
            }
        },
        {
            // Slow release and parallel blend for ambient volume swells.
            "Ambient Swell",
            {
                { params::threshold, -20.0f }, { params::ceiling, -1.0f },
                { params::attack, 2.0f }, { params::release, 420.0f },
                { params::knee, 12.0f }, { params::detectorHpf, 100.0f },
                { params::autoRelease, 1.0f },
                { params::body, 0.0f }, { params::air, 1.0f },
                { params::bite, 0.2f }, { params::mix, 0.65f }
            }
        },
        {
            // No soft knee, immediate attack: a last line of defence.
            "Safety Brickwall",
            {
                { params::threshold, -6.0f }, { params::ceiling, -0.3f },
                { params::attack, 0.05f }, { params::release, 80.0f },
                { params::knee, 0.0f }, { params::detectorHpf, 30.0f },
                { params::autoRelease, 0.0f },
                { params::body, 0.0f }, { params::air, 0.0f },
                { params::bite, 0.0f }, { params::mix, 1.0f }
            }
        }
    };

    return presets;
}

inline int getNumFactoryPresets()
{
    return static_cast<int> (getFactoryPresets().size());
}

inline std::string getFactoryPresetName (int index)
{
    const auto& presets = getFactoryPresets();

    if (index >= 0 && index < static_cast<int> (presets.size()))
        return presets[static_cast<size_t> (index)].name;

    return {};
}

} // namespace glimiter
