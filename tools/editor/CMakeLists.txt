set(EDITOR_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EDITOR_FORMATS AU)
endif()

juce_add_plugin(SampleEditor
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.sampleeditor"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CSed
    FORMATS ${EDITOR_FORMATS}
    PRODUCT_NAME "Sample Editor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Sample Editor records from your microphone or audio interface when you press Record."
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD FALSE)


target_sources(SampleEditor PRIVATE EditDocument.cpp EditorProcessor.cpp EditorView.cpp)
target_include_directories(SampleEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(SampleEditor
    PRIVATE
        crate_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
