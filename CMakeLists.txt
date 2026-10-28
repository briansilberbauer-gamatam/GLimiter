cmake_minimum_required(VERSION 3.22)

project(GLimiter VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Local dependency sysroots are build-time conveniences only. Never bake their
# absolute paths into distributed binaries.
set(CMAKE_SKIP_RPATH TRUE)

option(GLIMITER_BUILD_TESTS "Build the offline DSP test executable" ON)
option(GLIMITER_BUILD_SCREENSHOT "Build the headless editor renderer" ON)
option(GLIMITER_BUILD_HOST_TEST "Build the VST3 host smoke test" ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory(external/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(GLimiterAssets
    SOURCES
        Resources/LatoRegular.ttf
        Resources/LatoSemibold.ttf
        Resources/LatoBlack.ttf
        Resources/Lato-OFL.txt)

set(GLIMITER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters.h
    Source/Presets.h
    Source/Version.h
    Source/dsp/GuitarLimiter.h
    Source/ui/Theme.cpp
    Source/ui/Theme.h
    Source/ui/StudioLookAndFeel.cpp
    Source/ui/StudioLookAndFeel.h
    Source/ui/Controls.cpp
    Source/ui/Controls.h
    Source/ui/EffectPanel.cpp
    Source/ui/EffectPanel.h
    Source/ui/WaveformDisplay.cpp
    Source/ui/WaveformDisplay.h)

set(GLIMITER_DEFINITIONS
    GLIMITER_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

juce_add_plugin(GLimiter
    COMPANY_NAME "BLS Audio"
    BUNDLE_ID "com.blsaudio.glimiter"
    PLUGIN_MANUFACTURER_CODE Blsa
    PLUGIN_CODE Glmt
    FORMATS VST3 Standalone
    PRODUCT_NAME "GLimiter"
    DESCRIPTION "Guitar limiter with pick-preserving lookahead and tone shaping"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx")

target_sources(GLimiter PRIVATE ${GLIMITER_SOURCES})
target_include_directories(GLimiter PRIVATE Source)

target_compile_definitions(GLimiter PUBLIC ${GLIMITER_DEFINITIONS})

target_link_libraries(GLimiter
    PRIVATE
        GLimiterAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Lightweight standalone wrapper that shows the unit before touching the audio
# backend, so a stalled driver cannot leave the user staring at an empty desktop.
if(TARGET GLimiter_Standalone)
    target_sources(GLimiter_Standalone PRIVATE Source/Standalone/GLimiterStandaloneApp.cpp)
    target_include_directories(GLimiter_Standalone PRIVATE Source)
    target_compile_definitions(GLimiter_Standalone PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
        GLIMITER_VERSION="${PROJECT_VERSION}")
    target_link_libraries(GLimiter_Standalone
        PRIVATE
            juce::juce_audio_utils
            juce::juce_gui_extra)
endif()

if(GLIMITER_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(GLimiterTests PRODUCT_NAME "GLimiterTests")
    target_sources(GLimiterTests PRIVATE Tests/DspTests.cpp)
    target_include_directories(GLimiterTests PRIVATE Source)
    target_compile_definitions(GLimiterTests PRIVATE ${GLIMITER_DEFINITIONS})
    target_link_libraries(GLimiterTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME glimiter_dsp COMMAND $<TARGET_FILE:GLimiterTests>)
endif()

if(GLIMITER_BUILD_HOST_TEST)
    juce_add_console_app(GLimiterHostSmokeTest PRODUCT_NAME "GLimiterHostSmokeTest")

    target_sources(GLimiterHostSmokeTest PRIVATE Tools/HostSmokeTest.cpp)
    target_compile_definitions(GLimiterHostSmokeTest PRIVATE ${GLIMITER_DEFINITIONS} JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(GLimiterHostSmokeTest
        PRIVATE
            juce::juce_audio_utils
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_dependencies(GLimiterHostSmokeTest GLimiter_VST3)
endif()

if(GLIMITER_BUILD_SCREENSHOT)
    juce_add_gui_app(GLimiterScreenshot
        PRODUCT_NAME "GLimiter Screenshot"
        BUNDLE_ID "com.blsaudio.glimiterscreenshot"
        COMPANY_NAME "BLS Audio")

    target_sources(GLimiterScreenshot PRIVATE ${GLIMITER_SOURCES} Tools/RenderEditor.cpp)
    target_include_directories(GLimiterScreenshot PRIVATE Source)

    target_compile_definitions(GLimiterScreenshot PRIVATE ${GLIMITER_DEFINITIONS})

    target_link_libraries(GLimiterScreenshot
        PRIVATE
            GLimiterAssets
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
