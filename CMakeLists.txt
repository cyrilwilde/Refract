# ---------------------------------------------------------------------------
#  Refract - a spectral sample synthesiser
#  Build file. CMake reads this to generate an Xcode project (or Makefiles).
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22)

# Build a "universal binary": native code for Apple Silicon (arm64) AND Intel
# (x86_64) in one plugin. On your M2 the arm64 half runs - no Rosetta.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures to build")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the plugin supports")

project(Refract VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download JUCE automatically the first time you configure (needs internet once).
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Refract
    COMPANY_NAME             "Refract Audio"       # change to your name/brand
    BUNDLE_ID                com.refractaudio.refract
    PLUGIN_MANUFACTURER_CODE Rfau                  # 4 chars, at least one capital
    PLUGIN_CODE              Rfr1                  # 4 chars, unique per plugin
    PRODUCT_NAME             "Refract"
    FORMATS                  AU VST3 Standalone
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    VST3_CATEGORIES          Instrument Sampler Synth
    COPY_PLUGIN_AFTER_BUILD  TRUE)                 # installs into ~/Library/Audio/Plug-Ins

target_sources(Refract PRIVATE
    Source/Parameters.cpp
    Source/SpectralProcessor.cpp
    Source/SynthVoice.cpp
    Source/PluginProcessor.cpp
    Source/SpectralEditor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Refract PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Refract
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
