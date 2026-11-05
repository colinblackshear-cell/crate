cmake_minimum_required(VERSION 3.22)

project(Crate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Pass -DCRATE_JUCE_DIR=/path/to/JUCE (and CRATE_STRETCH_DIR) to build from local copies
# instead of downloading them.
set(CRATE_JUCE_DIR "" CACHE PATH "Local JUCE checkout (optional)")
set(CRATE_STRETCH_DIR "" CACHE PATH "Local signalsmith-stretch checkout (optional)")
set(CRATE_VOCODER_DIR "" CACHE PATH "Local checkout of the vocoder repo (optional)")

include(FetchContent)

if(CRATE_JUCE_DIR)
    add_subdirectory(${CRATE_JUCE_DIR} JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# Time-stretching and pitch-shifting (MIT licence).
if(CRATE_STRETCH_DIR)
    add_subdirectory(${CRATE_STRETCH_DIR} signalsmith-stretch)
else()
    FetchContent_Declare(signalsmith-stretch
        GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
        GIT_TAG 1.4.0
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(signalsmith-stretch)
endif()

# The vocoder lives in its own repo (colinblackshear-cell/vocoder); the Rig builds its code in.
# Only its sources are used, not its own build. Bump the commit to pick up vocoder changes.
if(CRATE_VOCODER_DIR)
    set(vocoder_SOURCE_DIR ${CRATE_VOCODER_DIR})
else()
    FetchContent_Declare(vocoder
        GIT_REPOSITORY https://github.com/colinblackshear-cell/vocoder.git
        GIT_TAG c74faceca2ab2377b599a4203b60c2b1bf26572b
        SOURCE_SUBDIR not-a-folder)
    FetchContent_MakeAvailable(vocoder)
endif()
set(VOCODER_INCLUDE ${vocoder_SOURCE_DIR}/Source)
set(VOCODER_SOURCES
    ${vocoder_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${vocoder_SOURCE_DIR}/Source/PluginEditor.cpp
    ${vocoder_SOURCE_DIR}/Source/MainPanel.cpp
    ${vocoder_SOURCE_DIR}/Source/EasyPanel.cpp)
# Each tool defines the plugin wrappers' createPluginFilter(); in the Rig they're built together,
# so the vocoder's copy gets another name. Call this in each folder that builds the vocoder's code.
macro(crate_rename_vocoder_entry)
    set_source_files_properties(${vocoder_SOURCE_DIR}/Source/PluginProcessor.cpp
        PROPERTIES COMPILE_DEFINITIONS createPluginFilter=createVocoderPluginFilter)
endmacro()

enable_testing()

# Shared by every tool: sound files, the sound library, edits, effects and common UI pieces.
# It's an INTERFACE library so each tool compiles it alongside its own copy of the JUCE modules.
add_library(crate_core INTERFACE)
target_include_directories(crate_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(crate_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/AudioFiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/Edits.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/KeyboardSampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/SoundFx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/SoundLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/SoundBank.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/EffectsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/LibraryList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/WaveView.cpp)
target_compile_definitions(crate_core INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(crate_core INTERFACE
    signalsmith-stretch
    juce::juce_audio_utils
    juce::juce_dsp)

add_subdirectory(tools/editor)
add_subdirectory(tools/mpc)
add_subdirectory(tools/bass)
add_subdirectory(tools/synth)
add_subdirectory(tools/rig)
add_subdirectory(tests)
