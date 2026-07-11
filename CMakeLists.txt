cmake_minimum_required(VERSION 3.22)

project(Keys VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: sibling checkout by default (../JUCE); override with -DKEYS_JUCE_PATH=<path>.
set(KEYS_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to a JUCE checkout")
add_subdirectory("${KEYS_JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

# okstudio-juce-kit: shared code for the line. Added AFTER JUCE (it links JUCE modules).
set(OKSTUDIO_KIT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../okstudio-juce-kit" CACHE PATH "Path to okstudio-juce-kit")
add_subdirectory("${OKSTUDIO_KIT_PATH}" "${CMAKE_BINARY_DIR}/okstudio-juce-kit")
include("${OKSTUDIO_KIT_CMAKE_DIR}/OKStudioPlugin.cmake")

# Where dev builds drop the VST3 so the DAW picks it up (Ableton custom folder).
# build.ps1 overrides / disables this; CI turns it off.
if (WIN32)
    set(KEYS_VST3_COPY_DIR "$ENV{USERPROFILE}/Ableton/vst3" CACHE PATH "Folder the VST3 is copied to after build")
endif()
option(KEYS_COPY_PLUGIN "Copy the VST3 to KEYS_VST3_COPY_DIR after building" ON)

set(_keysCopy "")
if (KEYS_COPY_PLUGIN AND WIN32)
    set(_keysCopy COPY_DIR "${KEYS_VST3_COPY_DIR}")
endif()

okstudio_add_plugin(Keys
    PRODUCT_NAME  "Keys"
    PLUGIN_CODE   Keys
    BUNDLE_SUFFIX keys
    ${_keysCopy})

target_sources(Keys PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/PianoKeyboard.cpp)

target_compile_definitions(Keys PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    KEYS_VERSION="${PROJECT_VERSION}")

if (WIN32)
    target_link_libraries(Keys PRIVATE wintrust crypt32)
endif()

target_link_libraries(Keys
    PRIVATE
        okstudio_kit
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Unit tests (console, JUCE UnitTest). Not built by the plugin targets; build and run
# with: cmake --build build --target Keys_tests && ctest --test-dir build -C Release
enable_testing()
add_executable(Keys_tests tests/KeysTests.cpp)
target_include_directories(Keys_tests PRIVATE src)
target_link_libraries(Keys_tests PRIVATE okstudio_kit)
add_test(NAME Keys_tests COMMAND Keys_tests)
