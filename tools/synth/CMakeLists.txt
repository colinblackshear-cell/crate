# Just the app now: in Logic, Kaleidobox Synth is the box's own (tools/rig, a one-tool
# Kaleidobox), which took over this plugin's name and code so songs made with it still open.
set(SYNTH_FORMATS Standalone)

juce_add_plugin(KaleidoboxSynth
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.synth"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CSyn
    FORMATS ${SYNTH_FORMATS}
    PRODUCT_NAME "Kaleidobox Synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The app, plugin, Rig and tests all compile this list.
set(SYNTH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/SynthEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SynthProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SynthView.cpp
    PARENT_SCOPE)

target_sources(KaleidoboxSynth PRIVATE PluginEntry.cpp SynthEngine.cpp SynthProcessor.cpp SynthView.cpp)
target_include_directories(KaleidoboxSynth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(KaleidoboxSynth
    PRIVATE
        crate_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
