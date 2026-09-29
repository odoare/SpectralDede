cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# These MUST come before project(): that is where CMake configures the Apple
# toolchain and reads them. Set afterwards, CMAKE_OSX_DEPLOYMENT_TARGET is
# ignored outright and the architectures are unreliable.
#
# 10.13 is JUCE 8's floor. It has to be well below 11.0 or the plugin refuses
# to load on Catalina (10.15); the arm64 slice of a universal build is clamped
# up to 11.0 by the toolchain on its own, so this costs nothing.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

project(Dede VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is expected as a sibling of this repo; FxmeTools lives in-tree as a git
# submodule under lib/. The module is registered directly rather than through
# fxmetools_attach: Dede compiles none of the WDL sources. The band splitter
# runs on fxme::RealFft (so its levels match fxme::SpectrumAnalyzer exactly —
# both use the same transform, whose scaling matches what JUCE's dsp::FFT did
# before the core split), and the reverb engine it does take from WDL
# (WDL/verbengine.h) is header-only. The nested WDL submodule still has to be
# checked out.
#
# FxmeTools is split in two: the JUCE module below, and FxmeCore, the JUCE-free
# half under lib/FxmeTools/core. Core is a second include root for the very
# same <FxmeTools/...> spelling, so which side a header lives on is invisible
# here — but the target has to be added, or the headers that moved (the band
# splitter, the reverb, the spectrum analyzer) are simply not found.
add_subdirectory(../JUCE JUCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/core FxmeCore)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/FxmeTools)

# The module's own sources include core headers, and juce_add_module makes an
# INTERFACE target whose sources compile into the consumer, so core has to
# travel with it.
target_link_libraries(FxmeTools INTERFACE FxmeCore)

juce_add_plugin(Dede
    COMPANY_NAME                "FX-Mechanics"
    COMPANY_WEBSITE             "www.fx-mechanics.com"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 DEDE
    FORMATS                     AU VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PRODUCT_NAME                "Dede"
    DESCRIPTION                 "FX-Mechanics spectral multi-delay and reverb"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Delay Reverb
    AU_MAIN_TYPE                kAudioUnitType_Effect
    BUNDLE_ID                   "com.FXMechanics.Dede"
)

# Source files are discovered recursively so adding a new component does not
# require editing this file. Re-run the CMake configure after adding files.
file(GLOB_RECURSE DEDE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
)

target_sources(Dede PRIVATE ${DEDE_SOURCES})

# Make every subdirectory under Source/ an include root so headers can be
# included by name regardless of where a component lives.
file(GLOB DEDE_INCLUDE_DIRS LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*
)
list(FILTER DEDE_INCLUDE_DIRS EXCLUDE REGEX "\\.(c|cc|cpp|cxx|h|hpp|hxx|mm)$")
target_include_directories(Dede PRIVATE ${DEDE_INCLUDE_DIRS})

# Image assets (logo, artwork) and factory preset XML. The presets are
# APVTS-state XML saved by the plugin; PresetManager keeps every embedded
# "*_xml" resource whose root tag matches the APVTS state type ("Parameters")
# and ignores the rest, so images and presets can share one binary-data blob.
# DEDE_HAS_BINARY_DATA lets the processor fall back to a user-presets-only
# manager while the Assets folder is still empty.
file(GLOB DEDE_ASSETS CONFIGURE_DEPENDS
                      ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.png
                      ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.jpg
                      ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.xml)
if(DEDE_ASSETS)
    juce_add_binary_data(DedeBinaryData SOURCES ${DEDE_ASSETS})
    set_target_properties(DedeBinaryData PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(Dede PRIVATE DedeBinaryData)
    target_compile_definitions(Dede PRIVATE DEDE_HAS_BINARY_DATA=1)
else()
    target_compile_definitions(Dede PRIVATE DEDE_HAS_BINARY_DATA=0)
endif()

juce_generate_juce_header(Dede)

target_compile_definitions(Dede
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(Dede
    PRIVATE
        FxmeTools
        FxmeCore
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
