# Headless tests for the shared core and the Sample Editor's audio side.
juce_add_console_app(CrateTests PRODUCT_NAME "CrateTests")
target_sources(CrateTests PRIVATE
    CoreTests.cpp
    EditorTests.cpp
    KeysTests.cpp
    MpcTests.cpp
    SequencerTests.cpp
    TestMain.cpp
    ${MPC_SOURCES}
    ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
target_include_directories(CrateTests PRIVATE ${PROJECT_SOURCE_DIR}/tools/editor ${PROJECT_SOURCE_DIR}/tools/mpc)
target_compile_definitions(CrateTests PRIVATE
    JucePlugin_Name="Sample Editor"
    JUCE_APPLICATION_NAME_STRING="CrateTests"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")
target_link_libraries(CrateTests PRIVATE
    crate_core
    mpc_format
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME crate_tests COMMAND CrateTests)

# The MPC2000XL file format on its own. Set MPC_FIXTURES to a folder of files saved by an
# MPC2000XL (CI uses the vMPC2000XL demo kits) to also check that each one reads and writes
# back byte for byte.
add_executable(MpcFormatTests MpcFormatTests.cpp)
target_link_libraries(MpcFormatTests PRIVATE mpc_format)
add_test(NAME mpc_format_tests COMMAND MpcFormatTests)
