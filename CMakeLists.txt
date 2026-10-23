cmake_minimum_required(VERSION 3.22)

# Universal binaries on macOS must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()

# C is required: juce_graphics compiles the SheenBidi text shaper from .c sources.
project(Rumble VERSION 0.26.0 LANGUAGES C CXX)

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
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ----------------------------------------------------------------- licensing

# Offline licence keys, shared with the other Fuzzy Audio plug-ins (the same
# module as in Key Detector). See docs/LICENSING.md.
juce_add_module("${CMAKE_CURRENT_SOURCE_DIR}/modules/fuzzyaudio_licensing")

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
    COMPANY_NAME                "Fuzzy Audio"
    BUNDLE_ID                   "com.fuzzyaudio.rumble"
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

# Brand artwork compiled into the binary, reachable as BinaryData::logo_png.
# Inter (variable) is SIL OFL 1.1 -- see assets/fonts/Inter-OFL.txt, which
# must ship with any distribution of the plugin.
juce_add_binary_data(RumbleAssets SOURCES assets/logo.png assets/fonts/InterVariable.ttf)

# Binary data is linked into a plugin, so it must be position independent.
set_target_properties(RumbleAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(Rumble PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Rumble
    PRIVATE
        RumbleAssets
        fuzzyaudio_licensing
        juce::juce_audio_utils
        juce::juce_animation
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------- offline renderer

# Builds without JUCE; handy for auditioning the DSP core.
add_executable(rumble_render tools/offline_render.cpp)

# ------------------------------------------------------------------- tests

# DSP regression tests, also without JUCE. Run with `ctest --test-dir build`
# (add `-C Release` for Xcode / Visual Studio builds).
enable_testing()
add_executable(rumble_tests tools/engine_tests.cpp)
add_test(NAME engine COMMAND rumble_tests)

# Plugin-level tests (parameters, LFO assignments, state), linked against the
# plugin's shared code rather than rebuilding JUCE.
add_executable(rumble_plugin_tests tools/plugin_tests.cpp)
target_link_libraries(rumble_plugin_tests PRIVATE Rumble)
target_include_directories(rumble_plugin_tests PRIVATE $<TARGET_PROPERTY:Rumble,INCLUDE_DIRECTORIES>)
target_compile_definitions(rumble_plugin_tests PRIVATE $<TARGET_PROPERTY:Rumble,COMPILE_DEFINITIONS>)
target_compile_options(rumble_plugin_tests PRIVATE $<TARGET_PROPERTY:Rumble,COMPILE_OPTIONS>)
add_test(NAME plugin COMMAND rumble_plugin_tests)

# ------------------------------------------------------------ licence keygen

# Headless key generator: makes a key pair and signs/verifies licence key
# files. OFF by default; build it where you keep the private key:
#   cmake -B build -DRUMBLE_BUILD_KEYGEN=ON && cmake --build build --target RumbleKeygen
option(RUMBLE_BUILD_KEYGEN "Build the licence key generator CLI" OFF)
if(RUMBLE_BUILD_KEYGEN)
    juce_add_console_app(RumbleKeygen PRODUCT_NAME "RumbleKeygen")
    target_sources(RumbleKeygen PRIVATE tools/keygen/keygen.cpp)
    target_compile_definitions(RumbleKeygen PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(RumbleKeygen PRIVATE
        juce::juce_product_unlocking
        juce::juce_data_structures
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
