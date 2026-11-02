set(MPC_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MPC_FORMATS AU)
endif()

# Reading and writing the MPC2000XL's .SND and .PGM files. Plain C++, no JUCE, so the format
# tests build on their own.
add_library(mpc_format STATIC mpc/MpcFiles.cpp mpc/BankWriter.cpp)
target_include_directories(mpc_format PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mpc_format PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(MpcBankBuilder
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.mpc"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CMbb
    FORMATS ${MPC_FORMATS}
    PRODUCT_NAME "MPC Bank Builder"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Sampler Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The app and plugin code; the tests compile the same list.
set(MPC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/BankModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MpcProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MpcView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PadEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PadGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PadSoundEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SampleTools.cpp
    PARENT_SCOPE)

target_sources(MpcBankBuilder PRIVATE PluginEntry.cpp BankModel.cpp MpcProcessor.cpp MpcView.cpp PadEditor.cpp PadGrid.cpp
    PadSoundEditor.cpp SampleTools.cpp)
target_include_directories(MpcBankBuilder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MpcBankBuilder
    PRIVATE
        crate_core
        mpc_format
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
