# Headless tests for the shared core and the Sample Editor's audio side.
juce_add_console_app(CrateTests PRODUCT_NAME "CrateTests")
target_sources(CrateTests PRIVATE
    CoreTests.cpp
    EditorTests.cpp
    TestMain.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
target_include_directories(CrateTests PRIVATE ${PROJECT_SOURCE_DIR}/tools/editor)
target_compile_definitions(CrateTests PRIVATE
    JucePlugin_Name="Sample Editor"
    JUCE_APPLICATION_NAME_STRING="CrateTests"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")
target_link_libraries(CrateTests PRIVATE
    crate_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME crate_tests COMMAND CrateTests)
