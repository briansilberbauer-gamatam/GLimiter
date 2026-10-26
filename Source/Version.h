#pragma once

// Injected by CMake from project(VERSION ...) so the editor, standalone host,
// and tooling all report the same number as the VST3 bundle metadata.
#ifndef GLIMITER_VERSION
 #define GLIMITER_VERSION "0.0.0"
#endif

namespace glimiter
{
    inline constexpr const char* version = GLIMITER_VERSION;
} // namespace glimiter
