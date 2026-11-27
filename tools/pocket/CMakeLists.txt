# Kaleidobox Pocket: the iPhone companion (Record, Effects + Chop, Sounds). On iOS it also compiles
# the reusable sound engines (drums, MPC) for the later steps; the vocoder runs on every build. With -DCRATE_POCKET_DESKTOP=ON
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

# TestFlight / App Store: the version people see (CFBundleShortVersionString) and the build number
# (CFBundleVersion), which must go up with every upload. The TestFlight workflow passes its run number;
# a build on your own Mac stays at 1.
set(POCKET_VERSION "0.2.0")
set(POCKET_BUILD_NUMBER "1" CACHE STRING "Kaleidobox Pocket build number (CFBundleVersion); must rise with each TestFlight upload")

juce_add_gui_app(KaleidoboxPocket
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.kaleidobox.pocket"
    PRODUCT_NAME "Kaleidobox Pocket"
    VERSION "${POCKET_VERSION}"
    BUILD_VERSION "${POCKET_BUILD_NUMBER}"
    # The iPhone icon is full-bleed with no transparency (the App Store refuses an icon with an alpha
    # channel; iOS rounds the corners itself). Drawn by tools/rig/icon/make_icon.py --phone.
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icon/pocket-icon.png"
    # An iPhone app (it runs on an iPad in iPhone mode). Portrait only, so it can't take part in iPad
    # split screen; saying so keeps App Store Connect from refusing the upload.
    TARGETED_DEVICE_FAMILY "1"
    REQUIRES_FULL_SCREEN TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kaleidobox Pocket records sounds from your microphone."
    BACKGROUND_AUDIO_ENABLED TRUE
    # Documents show up in the Files app: Sounds/*.wav and sounds.json.
    FILE_SHARING_ENABLED TRUE
    DOCUMENT_BROWSER_ENABLED TRUE
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait
    # Link to Mac: finds Kaleidobox on the Wi-Fi with Bonjour (iOS asks once for the local network).
    # ITSAppUsesNonExemptEncryption = false: the app has no encryption of its own (the Mac link is plain
    # local Wi-Fi), so App Store Connect doesn't ask the export-compliance question on every upload.
    PLIST_TO_MERGE "<plist><dict><key>NSLocalNetworkUsageDescription</key><string>Kaleidobox Pocket finds Kaleidobox on your Mac over the Wi-Fi, to send it sounds and run its Remote.</string><key>NSBonjourServices</key><array><string>_kaleidobox._tcp</string></array><key>ITSAppUsesNonExemptEncryption</key><false/></dict></plist>")

target_sources(KaleidoboxPocket PRIVATE
    Main.cpp
    PocketApp.cpp
    BeatsEngine.cpp
    BeatsScreen.cpp
    PadSheet.cpp
    PocketHold.cpp
    PocketSheet.cpp
    PocketDrawer.cpp
    PocketAudio.cpp
    PocketKit.cpp
    PocketLibrary.cpp
    PocketIdeas.cpp
    PocketFactory.cpp
    PocketLoopFx.cpp
    LoopEditSheet.cpp
    PocketLoopTools.cpp
    PocketChordPlayer.cpp
    SingScreen.cpp
    PocketFrame.cpp
    PocketIntro.cpp
    PocketLesson.cpp
    SoundBankSheet.cpp
    DrumKitSheet.cpp
    TempoSheet.cpp
    IdeasScreen.cpp
    PocketTheme.cpp
    PocketWave.cpp
    EffectsSheet.cpp
    LoopsScreen.cpp
    PocketLooper.cpp
    PocketSong.cpp
    RecordScreen.cpp
    SamplerScreen.cpp
    SoundsScreen.cpp
    PocketVocoder.cpp
    PocketSynth.cpp
    SynthScreen.cpp
    PocketChords.cpp
    ChordsScreen.cpp
    ChordEditSheet.cpp
    PocketDrone.cpp
    DroneScreen.cpp
    SongScreen.cpp
    TrackInSheet.cpp
    VocoderScreen.cpp
    PocketLinkClient.cpp
    LinkScreens.cpp
    PocketFxChecks.cpp
    PocketBoards.cpp
    EffectsScreen.cpp
    PocketAppEffects.cpp
    PocketAppNav.cpp
    PocketAppSong.cpp
    PocketAppTrack.cpp
    PocketAppSampler.cpp
    ${PROJECT_SOURCE_DIR}/core/link/PocketLink.cpp
    ${VOCODER_SOURCES})

# The Vocoder tab runs the real Kaleidobox vocoder (its own repo, built in like the Mac's Rig does).
crate_rename_vocoder_entry()
target_include_directories(KaleidoboxPocket PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${VOCODER_INCLUDE})

# Whether the sound is going to headphones: the phone asks the audio session, a computer says yes.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(KaleidoboxPocket PRIVATE PocketRoute.mm)
else()
    # (and POCKET_STRESS, the desktop preview's stress mode)
    target_sources(KaleidoboxPocket PRIVATE PocketRoute.cpp PocketStress.cpp)
endif()

target_compile_definitions(KaleidoboxPocket PRIVATE
    JucePlugin_Name="Kaleidobox Pocket"
    JUCE_APPLICATION_NAME_STRING="Kaleidobox Pocket"
    JUCE_APPLICATION_VERSION_STRING="${POCKET_VERSION}"
    JUCE_MODAL_LOOPS_PERMITTED=0)

# The SYNTH tool: the Mac's PS-8 synth engine (its DSP only, no plugin or window), on every build.
target_sources(KaleidoboxPocket PRIVATE ${TOOLS}/synth/SynthEngine.cpp)

# The EFFECTS tool: the Mac's pedalboard (the StripFx rack and its pedals, factory boards and tuner), its DSP only.
# The phone hosts no plugins, so the rack's plugin slot is left out.
target_sources(KaleidoboxPocket PRIVATE ${TOOLS}/rig/StripFx.cpp ${TOOLS}/rig/Pedalboard.cpp)
# CHORDS > EDIT CHORD names the chord you build with the Mac chord builder's own namer (RigChords::nameOf: no plugins, no routing)
target_sources(KaleidoboxPocket PRIVATE ${TOOLS}/rig/RigChords.cpp)
# SONG > BOUNCE writes MP3 with the Mac's encoder hookup
target_sources(KaleidoboxPocket PRIVATE ${TOOLS}/rig/Mp3Writer.cpp)
# SONG > FRIEND'S TRACK plays a whole song under yours with the Mac's own player (RigFriendTrack: no engine, no UI)
target_sources(KaleidoboxPocket PRIVATE ${TOOLS}/rig/RigFriendTrack.cpp)
target_include_directories(KaleidoboxPocket PRIVATE ${TOOLS}/rig)
target_compile_definitions(KaleidoboxPocket PRIVATE CRATE_STRIPFX_NO_PLUGINS=1)

# The Beats tab: the Mac's 808 engine and the MPC file writer. On the desktop preview the Mac tools
# already define mpc_format; on iOS the phone makes it here.
target_sources(KaleidoboxPocket PRIVATE ${TOOLS}/drums/DrumEngine.cpp ${TOOLS}/drums/FactoryKits.cpp ${TOOLS}/drums/SampleKits.cpp)
target_include_directories(KaleidoboxPocket PRIVATE ${TOOLS} ${TOOLS}/drums ${TOOLS}/mpc)
if(NOT TARGET mpc_format)
    add_library(mpc_format STATIC ${TOOLS}/mpc/mpc/MpcFiles.cpp ${TOOLS}/mpc/mpc/BankWriter.cpp)
    target_include_directories(mpc_format PUBLIC ${TOOLS}/mpc)
    set_target_properties(mpc_format PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_link_libraries(KaleidoboxPocket PRIVATE mpc_format)

# -DCRATE_POCKET_PHONE_EXTRAS=ON links these into the desktop preview too, so it links exactly what the phone links
# (a class defined twice under one name only breaks the build that has both: see BeatsScreen.cpp's StepGrid).
option(CRATE_POCKET_PHONE_EXTRAS "Desktop preview: also link the engine sources only the iPhone build links" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CRATE_POCKET_PHONE_EXTRAS)
    # The sound engines for later steps (the pad player). They only need to compile for now.

    target_sources(KaleidoboxPocket PRIVATE
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
        ${PROJECT_SOURCE_DIR}/core/control/Controllers.cpp)
endif()

target_link_libraries(KaleidoboxPocket
    PRIVATE
        crate_core
        crate_shine
        pocket_fonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
