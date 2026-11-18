# Kaleidobox Pocket: the iPhone companion (Record, Effects + Chop, Sounds). On iOS it also compiles
# the reusable sound engines (drums, MPC, vocoder) for the later steps. With -DCRATE_POCKET_DESKTOP=ON
# the same screens build as a 390x844 desktop window, for checking the look without a phone.
set(TOOLS ${PROJECT_SOURCE_DIR}/tools)

# The Mac app's own fonts (all SIL Open Font License; see tools/rig/fonts/LICENSES.txt). The phone
# build doesn't add tools/rig, so Pocket carries its own copy of the binary data.
juce_add_binary_data(pocket_fonts
    HEADER_NAME PocketFonts.h
    NAMESPACE PocketFonts
    SOURCES
        ${TOOLS}/rig/fonts/BarlowCondensed-SemiBold.ttf
        ${TOOLS}/rig/fonts/BarlowCondensed-Bold.ttf
        ${TOOLS}/rig/fonts/BarlowCondensed-ExtraBold.ttf
        ${TOOLS}/rig/fonts/Barlow-SemiBold.ttf
        ${TOOLS}/rig/fonts/VT323-Regular.ttf
        ${TOOLS}/rig/fonts/Fraunces-ExtraBoldItalic.ttf)
set_target_properties(pocket_fonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

juce_add_gui_app(KaleidoboxPocket
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.kaleidobox.pocket"
    PRODUCT_NAME "Kaleidobox Pocket"
    ICON_BIG "${TOOLS}/rig/icon/kaleidobox-icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kaleidobox Pocket records sounds from your microphone."
    BACKGROUND_AUDIO_ENABLED TRUE
    # Documents show up in the Files app: Sounds/*.wav and sounds.json.
    FILE_SHARING_ENABLED TRUE
    DOCUMENT_BROWSER_ENABLED TRUE
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait)

target_sources(KaleidoboxPocket PRIVATE
    Main.cpp
    PocketApp.cpp
    PocketAudio.cpp
    PocketLibrary.cpp
    PocketTheme.cpp
    PocketWave.cpp
    EffectsSheet.cpp
    RecordScreen.cpp
    SoundsScreen.cpp)

target_include_directories(KaleidoboxPocket PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(KaleidoboxPocket PRIVATE
    JucePlugin_Name="Kaleidobox Pocket"
    JUCE_APPLICATION_NAME_STRING="Kaleidobox Pocket"
    JUCE_APPLICATION_VERSION_STRING="0.2.0"
    JUCE_MODAL_LOOPS_PERMITTED=0)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # The sound engines for later steps (pads, vocoder). They only need to compile for now.
    add_library(mpc_format STATIC ${TOOLS}/mpc/mpc/MpcFiles.cpp ${TOOLS}/mpc/mpc/BankWriter.cpp)
    target_include_directories(mpc_format PUBLIC ${TOOLS}/mpc)
    set_target_properties(mpc_format PROPERTIES POSITION_INDEPENDENT_CODE ON)

    crate_rename_vocoder_entry()

    target_sources(KaleidoboxPocket PRIVATE
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
    target_include_directories(KaleidoboxPocket PRIVATE ${TOOLS}/drums ${TOOLS}/mpc ${VOCODER_INCLUDE})
    target_link_libraries(KaleidoboxPocket PRIVATE mpc_format)
endif()

target_link_libraries(KaleidoboxPocket
    PRIVATE
        crate_core
        pocket_fonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
