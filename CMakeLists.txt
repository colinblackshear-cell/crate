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

# MP3 out for Bounce + stems (Mac and phone): the Shine encoder (LGPL 2; github.com/toots/shine), built as
# its own small library. Linking crate_shine turns MP3 on in tools/rig/Mp3Writer.cpp (CRATE_HAS_SHINE).
FetchContent_Declare(shine
    GIT_REPOSITORY https://github.com/toots/shine.git
    GIT_TAG 7612c31e21e1fae2241ed86bc59c68d7b8f64e13
    SOURCE_SUBDIR not-a-folder)
FetchContent_MakeAvailable(shine)
add_library(crate_shine STATIC
    ${shine_SOURCE_DIR}/src/lib/bitstream.c
    ${shine_SOURCE_DIR}/src/lib/huffman.c
    ${shine_SOURCE_DIR}/src/lib/l3bitstream.c
    ${shine_SOURCE_DIR}/src/lib/l3loop.c
    ${shine_SOURCE_DIR}/src/lib/l3mdct.c
    ${shine_SOURCE_DIR}/src/lib/l3subband.c
    ${shine_SOURCE_DIR}/src/lib/layer3.c
    ${shine_SOURCE_DIR}/src/lib/reservoir.c
    ${shine_SOURCE_DIR}/src/lib/tables.c
    ${shine_SOURCE_DIR}/src/lib/precomputed_tables.c)
target_include_directories(crate_shine PUBLIC ${shine_SOURCE_DIR}/src/lib)
target_compile_definitions(crate_shine INTERFACE CRATE_HAS_SHINE=1)
set_target_properties(crate_shine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(crate_shine PRIVATE -w)
endif()

# The vocoder lives in its own repo (colinblackshear-cell/vocoder); the Rig builds its code in.
# Only its sources are used, not its own build. Bump the commit to pick up vocoder changes.
if(CRATE_VOCODER_DIR)
    set(vocoder_SOURCE_DIR ${CRATE_VOCODER_DIR})
else()
    FetchContent_Declare(vocoder
        GIT_REPOSITORY https://github.com/colinblackshear-cell/vocoder.git
        GIT_TAG 194d7b01f1f8484c64e090107227a75618211d4f
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
    ${CMAKE_CURRENT_SOURCE_DIR}/core/TempoFilter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/SoundSet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/EffectsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/LibraryList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/WaveView.cpp
    # Kaleidobox Visuals (the light show, on the graphics chip): the same on the Mac and the phone.
    ${CMAKE_CURRENT_SOURCE_DIR}/core/visuals/VisualAnalysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/visuals/VisualsRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/visuals/VisualsView.cpp)
target_compile_definitions(crate_core INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(crate_core INTERFACE
    signalsmith-stretch
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_opengl)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iPhone: only the phone app, built from the engine sources. The desktop tools host AU/VST3
    # plugins, which iOS doesn't have.
    add_subdirectory(tools/pocket)
else()
    add_subdirectory(tools/editor)
    add_subdirectory(tools/mpc)
    add_subdirectory(tools/bass)
    add_subdirectory(tools/synth)
    add_subdirectory(tools/drums)
    add_subdirectory(tools/rig)
    add_subdirectory(tests)
    # Optional: Kaleidobox Pocket's screens as a desktop window, to check the look without a phone.
    option(CRATE_POCKET_DESKTOP "Build the Kaleidobox Pocket UI as a 390x844 desktop app" OFF)
    if(CRATE_POCKET_DESKTOP)
        add_subdirectory(tools/pocket)
    endif()
endif()
