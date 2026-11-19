cmake_minimum_required(VERSION 3.22)

project(NoStringsVST VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# DSP core: plain C++17, header-only, no framework. Shared by the plugin, the
# console tools and (as a JS twin) the web build.
# ---------------------------------------------------------------------------
add_library(NoStringsCore INTERFACE)
target_include_directories(NoStringsCore INTERFACE core)

# ---------------------------------------------------------------------------
# NoStringsRender: renders test phrases to WAV and measures the core.
# Core only, so it rebuilds in seconds without touching the plugin.
# ---------------------------------------------------------------------------
add_executable(NoStringsRender tests/RenderWav.cpp tests/Stage1.cpp tests/Stage2.cpp tests/Stage3.cpp
                               tests/Stage4.cpp tests/Stage5.cpp tests/Stage6.cpp)
target_include_directories(NoStringsRender PRIVATE tests)
target_link_libraries(NoStringsRender PRIVATE NoStringsCore)

# ---------------------------------------------------------------------------
# NoStringsGenParams: writes ui/js/params.js from core/ParamTable.h.
# build.bat runs it before building the plugin, so the UI always matches.
# ---------------------------------------------------------------------------
add_executable(NoStringsGenParams tests/GenParamsJs.cpp)
target_link_libraries(NoStringsGenParams PRIVATE NoStringsCore)
target_compile_definitions(NoStringsGenParams PRIVATE _CRT_SECURE_NO_WARNINGS)
add_custom_target(GenParams
    COMMAND NoStringsGenParams "${CMAKE_CURRENT_SOURCE_DIR}/ui/js/params.js"
    DEPENDS NoStringsGenParams
    COMMENT "Generating ui/js/params.js from core/ParamTable.h")

foreach(t NoStringsRender NoStringsGenParams)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if NOSTRINGS_JUCE_DIR is given, otherwise fetch it.
# ---------------------------------------------------------------------------
option(NOSTRINGS_BUILD_PLUGIN "Build the VST3 / Standalone plugin (needs JUCE)" ON)

if(NOSTRINGS_BUILD_PLUGIN)
    set(NOSTRINGS_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")
    set(NOSTRINGS_JUCE_TAG "8.0.17-security" CACHE STRING "JUCE git tag to fetch")

    if(NOSTRINGS_JUCE_DIR AND EXISTS "${NOSTRINGS_JUCE_DIR}/CMakeLists.txt")
        add_subdirectory("${NOSTRINGS_JUCE_DIR}" JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${NOSTRINGS_JUCE_TAG}
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    # -----------------------------------------------------------------------
    # Plugin
    # -----------------------------------------------------------------------
    juce_add_plugin(NoStringsVST
        COMPANY_NAME            "Blink Zero"
        PRODUCT_NAME            "No Strings"
        PLUGIN_MANUFACTURER_CODE Blnk
        PLUGIN_CODE             NoSt
        BUNDLE_ID               com.blinkzero.nostrings
        FORMATS                 VST3 Standalone
        IS_SYNTH                TRUE
        NEEDS_MIDI_INPUT        TRUE
        NEEDS_MIDI_OUTPUT       FALSE
        IS_MIDI_EFFECT          FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES         Instrument Synth
        NEEDS_WEBVIEW2          TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    # The UI (HTML/CSS/JS) and the 64 factory presets are compiled into the plugin binary.
    file(GLOB_RECURSE NOSTRINGS_UI_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.js")
    juce_add_binary_data(NoStringsUI SOURCES ${NOSTRINGS_UI_FILES} presets/factory_presets.json)

    target_sources(NoStringsVST PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PresetManager.cpp)

    target_include_directories(NoStringsVST PRIVATE Source core)

    target_compile_definitions(NoStringsVST PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(NoStringsVST
        PRIVATE
            NoStringsUI
            NoStringsCore
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
    # -----------------------------------------------------------------------
    # Console tests for the preset system + the factory level matcher:
    #   cmake --build <build> --config Release --target NoStringsTests
    # -----------------------------------------------------------------------
    juce_add_console_app(NoStringsTests PRODUCT_NAME "NoStringsTests")
    target_sources(NoStringsTests PRIVATE tests/PresetTests.cpp Source/PresetManager.cpp)
    target_include_directories(NoStringsTests PRIVATE Source core)
    target_compile_definitions(NoStringsTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(NoStringsTests PRIVATE NoStringsUI NoStringsCore juce::juce_events
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
