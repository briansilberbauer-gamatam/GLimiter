#pragma once

#include <array>

namespace glimiter::params
{

// --- Global ---
inline constexpr auto inputGain  = "inputGain";
inline constexpr auto outputGain = "outputGain";
inline constexpr auto mix        = "mix";
inline constexpr auto bypass     = "bypass";

// --- Limiter ---
inline constexpr auto limiterOn    = "limiterOn";
inline constexpr auto threshold    = "threshold";
inline constexpr auto ceiling      = "ceiling";
inline constexpr auto attack       = "attack";
inline constexpr auto release      = "release";
inline constexpr auto knee         = "knee";
inline constexpr auto detectorHpf  = "detectorHpf";
inline constexpr auto autoRelease  = "autoRelease";

// --- Guitar shaping ---
inline constexpr auto shapeOn = "shapeOn";
inline constexpr auto body    = "body";
inline constexpr auto air     = "air";
inline constexpr auto bite    = "bite";

inline constexpr std::array<const char*, 16> allParameterIDs
{
    inputGain, outputGain, mix, bypass,
    limiterOn, threshold, ceiling, attack, release, knee, detectorHpf, autoRelease,
    shapeOn, body, air, bite
};

} // namespace glimiter::params
