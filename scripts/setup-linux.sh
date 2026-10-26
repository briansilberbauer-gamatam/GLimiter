#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Reuse the sibling plugin_poc dependency cache when it is available. This keeps
# the default dsh workspace-write sandbox offline and fast.
if [ ! -e external/JUCE ]; then
    mkdir -p external
    if [ -f ../plugin_poc/external/JUCE/CMakeLists.txt ]; then
        ln -sfn "$(cd ../plugin_poc/external/JUCE && pwd)" external/JUCE
        echo "Linked external/JUCE -> ../plugin_poc/external/JUCE"
    else
        git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git external/JUCE
    fi
fi

if [ ! -e deps/sysroot ]; then
    if [ -d ../plugin_poc/deps/sysroot/usr ]; then
        ln -sfn "$(cd ../plugin_poc/deps/sysroot && pwd)" deps/sysroot
        echo "Linked deps/sysroot -> ../plugin_poc/deps/sysroot"
    else
        mkdir -p deps
        echo "No local sysroot found. Run scripts/build.sh once the Linux -dev packages are installed." >&2
    fi
fi

echo "Dependencies are ready. Run scripts/build.sh next."
