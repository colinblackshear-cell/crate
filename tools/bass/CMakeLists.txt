set(BASS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BASS_FORMATS AU)
endif()

juce_add_plugin(CrateBass
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.bass"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CBas
    FORMATS ${BASS_FORMATS}
    PRODUCT_NAME "Crate Bass"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The app, plugin, Rig and tests all compile this list.
set(BASS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/BassEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BassProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BassView.cpp
    PARENT_SCOPE)

target_sources(CrateBass PRIVATE PluginEntry.cpp BassEngine.cpp BassProcessor.cpp BassView.cpp)
target_include_directories(CrateBass PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CrateBass
    PRIVATE
        crate_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
