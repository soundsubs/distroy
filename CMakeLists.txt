cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel) -- has no effect on
# other platforms. MUST be set before project(), since CMake configures
# the compiler/toolchain during that call. Checking CMAKE_HOST_SYSTEM_NAME
# rather than the APPLE variable here specifically because APPLE isn't
# reliably populated until AFTER project() runs -- a real catch-22 an
# earlier version of this file didn't account for. This project isn't
# cross-compiled (built on a Mac, for a Mac), so host==target and this
# check is equivalent to "is this macOS" in practice.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(DISTROYBOY VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Expects a JUCE checkout as a sibling directory by default:
#   your-workspace/
#     JUCE/            <- git clone https://github.com/juce-framework/JUCE.git
#     distroy-vst3/    <- this project
# Override with: cmake -B build -DJUCE_DIR=/path/to/JUCE
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
endif()

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${JUCE_DIR}.\n"
        "Clone it first: git clone https://github.com/juce-framework/JUCE.git\n"
        "as a sibling of this project folder, or pass -DJUCE_DIR=/path/to/JUCE")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(DISTROYBOY
    COMPANY_NAME "soundsubs"
    PLUGIN_MANUFACTURER_CODE Sndb
    PLUGIN_CODE DsBy
    FORMATS VST3
    PRODUCT_NAME "DISTROYBOY"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx" "Distortion"
)

target_sources(DISTROYBOY PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/distroy_dsp.c
)

# distroy_dsp.c is plain C -- make sure it's compiled as C, not swept
# into C++ compilation rules (CMake infers language from extension
# automatically for .c files, this is just an explicit safety net).
set_source_files_properties(Source/DSP/distroy_dsp.c PROPERTIES LANGUAGE C)

target_compile_definitions(DISTROYBOY PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DISTROYBOY PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_include_directories(DISTROYBOY PRIVATE Source Source/DSP)
