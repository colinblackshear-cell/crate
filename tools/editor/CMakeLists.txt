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

# The instrument version for Logic: the same editor, listed under Instruments, so a software
# instrument track's keys play the sound straight away. It has no audio input, so it can't
# record; record in the app or the effect version, or open a sound from the library.
set(INSTRUMENT_FORMATS VST3)
if(APPLE)
    list(APPEND INSTRUMENT_FORMATS AU)
endif()

juce_add_plugin(SampleInstrument
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.sampleinstrument"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CSei
    FORMATS ${INSTRUMENT_FORMATS}
    PRODUCT_NAME "Sample Editor Instrument"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SampleInstrument PRIVATE EditDocument.cpp EditorProcessor.cpp EditorView.cpp)
target_include_directories(SampleInstrument PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(SampleInstrument PRIVATE CRATE_INSTRUMENT=1)

target_link_libraries(SampleInstrument
    PRIVATE
        crate_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
