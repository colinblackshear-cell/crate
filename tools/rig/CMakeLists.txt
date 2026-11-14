# The Rig, called Kaleidobox (it was Crate Rig): plays the Crate tools live in one window, on one clock. An app, and (below) a plugin
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
juce_add_gui_app(Kaleidobox
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.rig"
    PRODUCT_NAME "Kaleidobox"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icon/kaleidobox-icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kaleidobox samples from your microphone or audio interface when you record in the Sampler.")

# The Rig hosts your own plugins (Audio Units on the Mac, VST3): Arturia synths, effects.
add_library(rig_hosting INTERFACE)
if(APPLE)
    target_compile_definitions(rig_hosting INTERFACE JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1)
else()
    target_compile_definitions(rig_hosting INTERFACE JUCE_PLUGINHOST_VST3=1)
endif()

set(RIG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/RigEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigRouting.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StripFx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginSlot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PerformFx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StripFxPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigLooper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LooperView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LoopEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SongGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HelpPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigScenes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigTimeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigSong.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TimelineView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SongView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MoreView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MixerView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SamplerScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VocoderScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VocoderSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GrooveView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigRecorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigCapture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundsView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GuideView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Vintage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigPanels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Analog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/InstrumentShell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SynthScreens.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigSongStart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Pedalboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EffectsView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigQuantize.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/QuantizePanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RigChords.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ChordsView.cpp
    PARENT_SCOPE)

target_sources(Kaleidobox PRIVATE Main.cpp RigEngine.cpp RigRouting.cpp RigTransport.cpp RigView.cpp StripFx.cpp PluginSlot.cpp PerformFx.cpp StripFxPanel.cpp RigLooper.cpp LooperView.cpp LoopEditor.cpp SongGrid.cpp RigExport.cpp HelpPanel.cpp RigScenes.cpp RigTimeline.cpp RigSong.cpp TimelineView.cpp SongView.cpp MoreView.cpp MixerView.cpp SamplerScreen.cpp VocoderScreen.cpp VocoderSettings.cpp GrooveView.cpp RigRecorder.cpp RigCapture.cpp SoundsView.cpp GuideView.cpp Vintage.cpp RigPanels.cpp Analog.cpp InstrumentShell.cpp SynthScreens.cpp RigSongStart.cpp Pedalboard.cpp EffectsView.cpp RigQuantize.cpp QuantizePanel.cpp RigChords.cpp ChordsView.cpp
    ${MPC_SOURCES}
    ${VOCODER_SOURCES}
    ${BASS_SOURCES}
    ${SYNTH_SOURCES}
    ${DRUMS_SOURCES}
    ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
target_include_directories(Kaleidobox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tools/mpc ${PROJECT_SOURCE_DIR}/tools/editor ${VOCODER_INCLUDE}
    ${PROJECT_SOURCE_DIR}/tools/bass ${PROJECT_SOURCE_DIR}/tools/synth ${PROJECT_SOURCE_DIR}/tools/drums)
# The sampler is the Sample Editor's code and the vocoder is the Vocoder's; they use the plugin's
# name in a few places.
target_compile_definitions(Kaleidobox PRIVATE
    JucePlugin_Name="Sample Editor"
    JUCE_APPLICATION_NAME_STRING="Kaleidobox"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")

target_link_libraries(Kaleidobox
    PRIVATE
        crate_core
        mpc_format
        rig_fonts
        rig_hosting
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

juce_add_plugin(KaleidoboxPlugin
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.crate.rigplugin"
    PLUGIN_MANUFACTURER_CODE Cblk
    PLUGIN_CODE CRig
    FORMATS ${RIG_PLUGIN_FORMATS}
    PRODUCT_NAME "Kaleidobox"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icon/kaleidobox-icon.png"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kaleidobox hears your microphone through Logic's side chain."
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

# What every Kaleidobox plugin is made of (the instrument and the Input version share it all).
macro(kaleidobox_plugin_parts target)
    target_sources(${target} PRIVATE RigPlugin.cpp RigEngine.cpp RigRouting.cpp RigTransport.cpp RigView.cpp StripFx.cpp PluginSlot.cpp PerformFx.cpp StripFxPanel.cpp RigLooper.cpp LooperView.cpp LoopEditor.cpp SongGrid.cpp RigExport.cpp HelpPanel.cpp RigScenes.cpp RigTimeline.cpp RigSong.cpp TimelineView.cpp SongView.cpp MoreView.cpp MixerView.cpp SamplerScreen.cpp VocoderScreen.cpp VocoderSettings.cpp GrooveView.cpp RigRecorder.cpp RigCapture.cpp SoundsView.cpp GuideView.cpp Vintage.cpp RigPanels.cpp Analog.cpp InstrumentShell.cpp SynthScreens.cpp RigSongStart.cpp Pedalboard.cpp EffectsView.cpp RigQuantize.cpp QuantizePanel.cpp RigChords.cpp ChordsView.cpp
        ${MPC_SOURCES}
        ${VOCODER_SOURCES}
        ${BASS_SOURCES}
        ${SYNTH_SOURCES}
        ${DRUMS_SOURCES}
        ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
        ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
        ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tools/mpc ${PROJECT_SOURCE_DIR}/tools/editor ${VOCODER_INCLUDE}
        ${PROJECT_SOURCE_DIR}/tools/bass ${PROJECT_SOURCE_DIR}/tools/synth ${PROJECT_SOURCE_DIR}/tools/drums)
    target_link_libraries(${target}
        PRIVATE
            crate_core
            mpc_format
            rig_fonts
            rig_hosting
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endmacro()
kaleidobox_plugin_parts(KaleidoboxPlugin)

# "Kaleidobox Input": the same box as an AU MIDI-controlled effect, the kind of plugin Logic
# sends side chain audio into: on a Software Instrument track, pick the input in Side Chain, as
# with the Vocoder. Logic gives an instrument plugin (above) no side chain audio, so the Line in
# and the vocoder's voice stay silent there. No separate outputs per tool: Logic only has those for instruments.
if(APPLE)
    juce_add_plugin(KaleidoboxInputPlugin
        COMPANY_NAME "Colin Blackshear"
        BUNDLE_ID "com.colinblackshear.crate.rigplugin.input"
        PLUGIN_MANUFACTURER_CODE Cblk
        PLUGIN_CODE CRgI
        FORMATS AU
        PRODUCT_NAME "Kaleidobox Input"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icon/kaleidobox-icon.png"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Kaleidobox hears your microphone and instruments through Logic."
        AU_MAIN_TYPE kAudioUnitType_MusicEffect
        COPY_PLUGIN_AFTER_BUILD FALSE)
    kaleidobox_plugin_parts(KaleidoboxInputPlugin)
    target_compile_definitions(KaleidoboxInputPlugin PRIVATE RIG_PLUGIN_INPUT=1)
endif()

# The Sample Editor's plugin entry is built in too; the plugin's own is RigPlugin's.
set_source_files_properties(${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    PROPERTIES COMPILE_DEFINITIONS createPluginFilter=createSampleEditorPluginFilter)
