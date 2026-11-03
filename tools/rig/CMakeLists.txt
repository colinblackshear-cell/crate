# The Rig: plays the Crate tools live in one window, on one clock. A plain app (no plugin):
# for Logic, use each tool's own plugin.
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
    PARENT_SCOPE)

target_sources(CrateRig PRIVATE Main.cpp RigEngine.cpp RigRouting.cpp RigTransport.cpp RigView.cpp
    ${MPC_SOURCES}
    ${VOCODER_SOURCES}
    ${PROJECT_SOURCE_DIR}/tools/editor/EditDocument.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorProcessor.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/EditorView.cpp)
target_include_directories(CrateRig PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tools/mpc ${PROJECT_SOURCE_DIR}/tools/editor ${VOCODER_INCLUDE})
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
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

crate_rename_vocoder_entry()
