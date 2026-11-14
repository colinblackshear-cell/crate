# Just the app now: in Logic, Kaleidobox Bass is the box's own (tools/rig, a one-tool
# Kaleidobox), which took over this plugin's name and code so songs made with it still open.
set(BASS_FORMATS Standalone)

juce_add_plugin(KaleidoboxBass
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.bass"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CBas
    FORMATS ${BASS_FORMATS}
    PRODUCT_NAME "Kaleidobox Bass"
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

target_sources(KaleidoboxBass PRIVATE PluginEntry.cpp BassEngine.cpp BassProcessor.cpp BassView.cpp)
target_include_directories(KaleidoboxBass PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(KaleidoboxBass
    PRIVATE
        crate_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
