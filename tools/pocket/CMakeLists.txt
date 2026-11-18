# Kaleidobox Pocket: the iPhone companion. For now it just compiles the reusable sound engines
# (drums, MPC, vocoder) and plays a kick pattern; the real screens come later.
set(TOOLS ${PROJECT_SOURCE_DIR}/tools)

juce_add_gui_app(KaleidoboxPocket
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.kaleidobox.pocket"
    PRODUCT_NAME "Kaleidobox Pocket"
    ICON_BIG "${TOOLS}/rig/icon/kaleidobox-icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kaleidobox Pocket samples from your microphone and runs the vocoder on your voice."
    BACKGROUND_AUDIO_ENABLED TRUE
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait)

# The MPC file formats (plain C++).
add_library(mpc_format STATIC ${TOOLS}/mpc/mpc/MpcFiles.cpp ${TOOLS}/mpc/mpc/BankWriter.cpp)
target_include_directories(mpc_format PUBLIC ${TOOLS}/mpc)
set_target_properties(mpc_format PROPERTIES POSITION_INDEPENDENT_CODE ON)

crate_rename_vocoder_entry()

target_sources(KaleidoboxPocket PRIVATE
    Main.cpp
    PocketApp.cpp
    ${TOOLS}/drums/DrumEngine.cpp
    ${TOOLS}/drums/DrumProcessor.cpp
    ${TOOLS}/drums/DrumView.cpp
    ${TOOLS}/mpc/BankBrowser.cpp
    ${TOOLS}/mpc/BankModel.cpp
    ${TOOLS}/mpc/LivePanel.cpp
    ${TOOLS}/mpc/MpcProcessor.cpp
    ${TOOLS}/mpc/MpcView.cpp
    ${TOOLS}/mpc/PadEditor.cpp
    ${TOOLS}/mpc/PadGrid.cpp
    ${TOOLS}/mpc/PadSoundEditor.cpp
    ${TOOLS}/mpc/SampleTools.cpp
    ${TOOLS}/mpc/Sequence.cpp
    ${TOOLS}/mpc/SequencerEngine.cpp
    ${TOOLS}/mpc/SequencerView.cpp
    ${PROJECT_SOURCE_DIR}/core/control/Controllers.cpp
    ${VOCODER_SOURCES})

target_include_directories(KaleidoboxPocket PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${TOOLS}/drums ${TOOLS}/mpc ${VOCODER_INCLUDE})

# The vocoder and MPC code use the plugin's name in a few places.
target_compile_definitions(KaleidoboxPocket PRIVATE
    JucePlugin_Name="Kaleidobox Pocket"
    JUCE_APPLICATION_NAME_STRING="Kaleidobox Pocket"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(KaleidoboxPocket
    PRIVATE
        crate_core
        mpc_format
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
