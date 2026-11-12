set(DRUMS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DRUMS_FORMATS AU)
endif()

juce_add_plugin(KaleidoboxDrums
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.drums"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CDrm
    FORMATS ${DRUMS_FORMATS}
    PRODUCT_NAME "Kaleidobox Drums"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The app, plugin, Rig and tests all compile this list.
set(DRUMS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/DrumEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DrumProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DrumView.cpp
    PARENT_SCOPE)

target_sources(KaleidoboxDrums PRIVATE PluginEntry.cpp DrumEngine.cpp DrumProcessor.cpp DrumView.cpp)
target_include_directories(KaleidoboxDrums PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(KaleidoboxDrums
    PRIVATE
        crate_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
