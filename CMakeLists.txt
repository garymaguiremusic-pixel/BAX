cmake_minimum_required(VERSION 3.22)

project(BAXR VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(BAXR_BUILD_STANDALONE "Also build a standalone app for testing"                    OFF)
option(BAXR_BUILD_TESTS      "Build the test programs (DSP tests + plugin tests)"          OFF)
option(BAXR_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

# ------------------------------------------------------------------------------
# DSP tests: plain C++, no JUCE
if(BAXR_BUILD_TESTS)
    enable_testing()
    add_executable(BAXR_EngineTests tests/EngineTests.cpp)
    target_include_directories(BAXR_EngineTests PRIVATE Source)
    add_test(NAME EngineTests COMMAND BAXR_EngineTests)
endif()

# ------------------------------------------------------------------------------
# JUCE
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin
set(BAXR_FORMATS VST3)
if(BAXR_BUILD_STANDALONE)
    list(APPEND BAXR_FORMATS Standalone)
endif()

# The VST3 manifest helper has to run on the build machine.
if(CMAKE_CROSSCOMPILING)
    set(BAXR_VST3_MANIFEST FALSE)
else()
    set(BAXR_VST3_MANIFEST TRUE)
endif()

juce_add_plugin(BAXR
    PRODUCT_NAME                "BAXR"
    COMPANY_NAME                "BAXR Project"
    BUNDLE_ID                   com.baxrproject.baxr
    PLUGIN_MANUFACTURER_CODE    Bxrp
    PLUGIN_CODE                 Bxq1
    FORMATS                     ${BAXR_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx EQ
    VST3_AUTO_MANIFEST          ${BAXR_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD     ${BAXR_COPY_AFTER_BUILD})

set(BAXR_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CustomLookAndFeel.cpp
    Source/UI/Components.cpp)

target_sources(BAXR PRIVATE ${BAXR_SOURCES})
target_include_directories(BAXR PRIVATE Source)

set(BAXR_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(BAXR PUBLIC ${BAXR_DEFINITIONS})

target_link_libraries(BAXR
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Plugin tests: the processor + editor in a console app (parameters, state,
# latency, float/double processing, and a PNG render of the editor).
if(BAXR_BUILD_TESTS)
    juce_add_console_app(BAXR_PluginTests PRODUCT_NAME "BAXR_PluginTests")
    target_sources(BAXR_PluginTests PRIVATE tests/PluginTests.cpp ${BAXR_SOURCES})
    target_include_directories(BAXR_PluginTests PRIVATE Source)
    target_compile_definitions(BAXR_PluginTests PRIVATE ${BAXR_DEFINITIONS}
        JucePlugin_Name="BAXR" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(BAXR_PluginTests PRIVATE
        juce::juce_audio_processors juce::juce_gui_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME PluginTests COMMAND BAXR_PluginTests)
endif()
