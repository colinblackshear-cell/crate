# Headless tests for the shared core and the Sample Editor's audio side.
juce_add_console_app(CrateTests PRODUCT_NAME "CrateTests")
target_sources(CrateTests PRIVATE
    CoreTests.cpp
    VisualsTests.cpp
    DrumMachineTests.cpp
    DroneTests.cpp
    EditorTests.cpp
    KeysTests.cpp
    MpcTests.cpp
    MpcPerformTests.cpp
    RigTests.cpp
    RigFxTests.cpp
    RigLooperTests.cpp
    RigLoopSyncTests.cpp
    RigFriendTrackTests.cpp
    RigLoopCopyTests.cpp
    RigOverdubUndoTests.cpp
    RigOverdubUndoShowTests.cpp
    OverdubUndoBench.cpp
    RigDirtyTests.cpp
    RigDirtyScenarioTests.cpp
    RigDirtyPluginTests.cpp
    LoopShapeTests.cpp
    WarpTests.cpp
    RigLoopLabTests.cpp
    SoundBendingTests.cpp
    ShowMovesTests.cpp
    RigSceneTests.cpp
    RigTimelineTests.cpp
    RigSongTests.cpp
    RigGrooveTests.cpp
    RigSamplerScreenTests.cpp
    RigMpcUnitTests.cpp
    RigVocoderScreenTests.cpp
    RigRecorderTests.cpp
    RigPluginTests.cpp
    RigPluginScreensTests.cpp
    RigFirstRunTests.cpp
    RigSwitchTests.cpp
    RigVocoderWakeTests.cpp
    RigPluginSlotTests.cpp
    RigPerformFxTests.cpp
    RigSynthScreenTests.cpp
    RigGuideTests.cpp
    RigIdeasTests.cpp
    RigEffectsTests.cpp
    RigMixerTests.cpp
    RigNavigationTests.cpp
    RigFrameTests.cpp
    RigLoopBankTests.cpp
    RigQuantizeTests.cpp
    RigChordsTests.cpp
    RigToolPluginTests.cpp
    RigLogicImportTests.cpp
    RigSongCaptureTests.cpp
    RigTakesTests.cpp
    RigLiveSongTests.cpp
    RigNotesTests.cpp
    PatternLifeTests.cpp
    RigAutomationTests.cpp
    RigLevelTests.cpp
    RigControllersTests.cpp
    RigClearTests.cpp
    RigFactoryKitTests.cpp
    RigLessonTests.cpp
    RigPhoneLinkTests.cpp
    RigLayers16Tests.cpp
    RigShowFilesTests.cpp
    DroneRigTests.cpp
    NoteRepeatTests.cpp
    DrumStepToolsTests.cpp
    DrumSampleKitTests.cpp
    DrumStepToolsUiTests.cpp
    DrumKitUiTests.cpp
    ${PROJECT_SOURCE_DIR}/tools/pocket/PocketLinkClient.cpp
    ${PROJECT_SOURCE_DIR}/tools/pocket/BeatsEngine.cpp
    ${PROJECT_SOURCE_DIR}/tools/rig/RigPlugin.cpp
    SoundBankTests.cpp
    SynthTests.cpp
    SequencerTests.cpp
    TestMain.cpp
    ${MPC_SOURCES}
    ${RIG_SOURCES}
    ${VOCODER_SOURCES}
    ${BASS_SOURCES}
    ${SYNTH_SOURCES}
    ${DRUMS_SOURCES}
    ${PROJECT_SOURCE_DIR}/tools/drone/DroneProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
target_include_directories(CrateTests PRIVATE ${PROJECT_SOURCE_DIR}/tools/editor ${PROJECT_SOURCE_DIR}/tools/mpc ${PROJECT_SOURCE_DIR}/tools/rig ${VOCODER_INCLUDE}
    ${PROJECT_SOURCE_DIR}/tools/bass ${PROJECT_SOURCE_DIR}/tools/synth ${PROJECT_SOURCE_DIR}/tools/drums ${PROJECT_SOURCE_DIR}/tools/drone ${PROJECT_SOURCE_DIR}/tools)
target_compile_definitions(CrateTests PRIVATE
    JucePlugin_Name="Sample Editor"
    JUCE_APPLICATION_NAME_STRING="CrateTests"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    KALEIDOBOX_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(CrateTests PRIVATE
    crate_core
    crate_shine
    mpc_format
    rig_fonts
    rig_hosting
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
# The hosted-plugin dirty tests also host the repo's own instrument (and effect) as real VST3s
# when they've been built (the Mac's CI builds everything; Linux CI only the tests, and there
# that part is skipped). No build dependency: the tests don't wait for the plugins.
target_compile_definitions(CrateTests PRIVATE
    "CRATE_TEST_INSTRUMENT_VST3=\"$<GENEX_EVAL:$<TARGET_PROPERTY:SampleInstrument_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>\""
    "CRATE_TEST_EFFECT_VST3=\"$<GENEX_EVAL:$<TARGET_PROPERTY:SampleEditor_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>\"")
add_test(NAME crate_tests COMMAND CrateTests)
# A hung test fails the run instead of holding the runner for hours.
set_tests_properties(crate_tests PROPERTIES TIMEOUT 900)

# The MPC2000XL file format on its own. Set MPC_FIXTURES to a folder of files saved by an
# MPC2000XL (CI uses the vMPC2000XL demo kits) to also check that each one reads and writes
# back byte for byte.
add_executable(MpcFormatTests MpcFormatTests.cpp)
target_link_libraries(MpcFormatTests PRIVATE mpc_format)
add_test(NAME mpc_format_tests COMMAND MpcFormatTests)

crate_rename_vocoder_entry()
# The tests build the Rig plugin's code without being a plugin.
set_source_files_properties(${PROJECT_SOURCE_DIR}/tools/rig/RigPlugin.cpp
    PROPERTIES COMPILE_DEFINITIONS createPluginFilter=createRigPluginFilter)
