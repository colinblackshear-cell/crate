# The Rig: plays the Crate tools live in one window, on one clock. An app, and (below) a plugin
# for Logic.

# The panel's fonts (all SIL Open Font License; see fonts/LICENSES.txt): Barlow Condensed for
# labels and buttons, Barlow for small print, VT323 for the LCDs, Fraunces for the wordmark.
juce_add_binary_data(rig_fonts
    HEADER_NAME RigFonts.h
    NAMESPACE RigFonts
    SOURCES
        fonts/BarlowCondensed-SemiBold.ttf
        fonts/BarlowCondensed-Bold.ttf
        fonts/BarlowCondensed-ExtraBold.ttf
        fonts/Barlow-SemiBold.ttf
        fonts/VT323-Regular.ttf
        fonts/Fraunces-ExtraBoldItalic.ttf)
set_target_properties(rig_fonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
juce_add_gui_app(CrateRig
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.rig"
    PRODUCT_NAME "Crate Rig"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Crate Rig samples from your microphone or audio interface when you record in the Sampler.")

set(RIG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/RigEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigRouting.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StripFx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StripFxPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigLooper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LooperView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LoopEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SongGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigScenes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SongView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StageView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SamplerScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VocoderScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GrooveView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigRecorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigCapture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundsView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Vintage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigPanels.cpp
    PARENT_SCOPE)

target_sources(CrateRig PRIVATE Main.cpp RigEngine.cpp RigRouting.cpp RigTransport.cpp RigView.cpp StripFx.cpp StripFxPanel.cpp RigLooper.cpp LooperView.cpp LoopEditor.cpp SongGrid.cpp RigScenes.cpp SongView.cpp StageView.cpp SamplerScreen.cpp VocoderScreen.cpp GrooveView.cpp RigRecorder.cpp RigCapture.cpp SoundsView.cpp Vintage.cpp RigPanels.cpp
    ${MPC_SOURCES}
    ${VOCODER_SOURCES}
    ${BASS_SOURCES}
    ${SYNTH_SOURCES}
    ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
target_include_directories(CrateRig PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tools/mpc ${PROJECT_SOURCE_DIR}/tools/editor ${VOCODER_INCLUDE}
    ${PROJECT_SOURCE_DIR}/tools/bass ${PROJECT_SOURCE_DIR}/tools/synth)
# The sampler is the Sample Editor's code and the vocoder is the Vocoder's; they use the plugin's
# name in a few places.
target_compile_definitions(CrateRig PRIVATE
    JucePlugin_Name="Sample Editor"
    JUCE_APPLICATION_NAME_STRING="Crate Rig"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")

target_link_libraries(CrateRig
    PRIVATE
        crate_core
        mpc_format
        rig_fonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

crate_rename_vocoder_entry()

# The Rig as a Logic plugin: an instrument that runs the whole Rig in a Logic song. Same code
# as the app; RigPlugin is its host side (Logic's clock, MIDI, side chain and outputs).
set(RIG_PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND RIG_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(CrateRigPlugin
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.rigplugin"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CRig
    FORMATS ${RIG_PLUGIN_FORMATS}
    PRODUCT_NAME "Crate Rig"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Crate Rig hears your microphone through Logic's side chain."
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(CrateRigPlugin PRIVATE RigPlugin.cpp RigEngine.cpp RigRouting.cpp RigTransport.cpp RigView.cpp StripFx.cpp StripFxPanel.cpp RigLooper.cpp LooperView.cpp LoopEditor.cpp SongGrid.cpp RigScenes.cpp SongView.cpp StageView.cpp SamplerScreen.cpp VocoderScreen.cpp GrooveView.cpp RigRecorder.cpp RigCapture.cpp SoundsView.cpp Vintage.cpp RigPanels.cpp
    ${MPC_SOURCES}
    ${VOCODER_SOURCES}
    ${BASS_SOURCES}
    ${SYNTH_SOURCES}
    ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
target_include_directories(CrateRigPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tools/mpc ${PROJECT_SOURCE_DIR}/tools/editor ${VOCODER_INCLUDE}
    ${PROJECT_SOURCE_DIR}/tools/bass ${PROJECT_SOURCE_DIR}/tools/synth)
target_link_libraries(CrateRigPlugin
    PRIVATE
        crate_core
        mpc_format
        rig_fonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# The Sample Editor's plugin entry is built in too; the plugin's own is RigPlugin's.
set_source_files_properties(${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    PROPERTIES COMPILE_DEFINITIONS createPluginFilter=createSampleEditorPluginFilter)
