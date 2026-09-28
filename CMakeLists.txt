cmake_minimum_required(VERSION 3.22)

# Universal binaries on macOS must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()

project(Rumble VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Plugins must not depend on a separately installed MSVC runtime.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------- JUCE

# Use a local JUCE checkout if JUCE_PATH is given, otherwise fetch it.
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------- formats

# VST3 and Standalone build everywhere. AU is macOS-only. AAX needs Avid's
# SDK, which is under NDA and cannot be fetched automatically -- pass
# -DAAX_SDK_PATH=/path/to/aax-sdk to enable it.
set(RUMBLE_FORMATS VST3 Standalone)

if(APPLE)
    list(APPEND RUMBLE_FORMATS AU)
endif()

set(AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (enables the AAX build)")
if(AAX_SDK_PATH)
    if(NOT EXISTS "${AAX_SDK_PATH}/Interfaces/AAX.h")
        message(FATAL_ERROR
            "AAX_SDK_PATH='${AAX_SDK_PATH}' does not look like an AAX SDK "
            "(Interfaces/AAX.h not found).")
    endif()
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND RUMBLE_FORMATS AAX)
endif()

message(STATUS "Rumble: building formats -> ${RUMBLE_FORMATS}")

# -------------------------------------------------------------------- target

# CI builds artifacts for download and must not touch system plugin folders.
option(RUMBLE_COPY_PLUGIN "Install the built plugin into the system plugin folders" ON)

juce_add_plugin(Rumble
    COMPANY_NAME                "WaywardRoots"
    BUNDLE_ID                   "com.waywardroots.rumble"
    PLUGIN_MANUFACTURER_CODE    Wywd
    PLUGIN_CODE                 Rmbl
    FORMATS                     ${RUMBLE_FORMATS}
    PRODUCT_NAME                "Rumble"
    DESCRIPTION                 "Ducked dark reverb for techno rumble bass"

    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    VST3_CATEGORIES             "Fx" "Reverb"
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                AAX_ePlugInCategory_Reverb

    COPY_PLUGIN_AFTER_BUILD     ${RUMBLE_COPY_PLUGIN})

target_sources(Rumble PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(Rumble PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Rumble
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------- offline renderer

# Builds without JUCE; handy for auditioning the DSP core.
add_executable(rumble_render tools/offline_render.cpp)
