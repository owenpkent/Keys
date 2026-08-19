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

set(KEYS_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/mcp/KeysMcp.cpp
    src/ui/NoteSurface.cpp
    src/ui/PianoKeyboard.cpp
    src/ui/KeysLookAndFeel.cpp
    src/ui/KnobBank.cpp
    src/ui/ChordPads.cpp
    src/ui/ChordGenMenu.cpp
    src/ui/ChordGenPanel.cpp
    src/ui/ChordTray.cpp
    src/ui/ChordLibraryPanel.cpp
    src/ui/SourceViz.cpp
    src/ui/TakePanel.cpp
    src/ui/ArpPanel.cpp)

# Sources, compile defs and links are identical for the instrument and the MIDI-effect
# variant; only the plugin classification (set by okstudio_add_plugin) and the
# KEYS_MIDI_EFFECT gate differ. Configure both through one helper.
function(keys_configure_target tgt)
    target_sources(${tgt} PRIVATE ${KEYS_SOURCES})

    target_compile_definitions(${tgt} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        KEYS_VERSION="${PROJECT_VERSION}")

    if (WIN32)
        target_link_libraries(${tgt} PRIVATE wintrust crypt32)
    endif()

    target_link_libraries(${tgt}
        PRIVATE
            okstudio_kit
            juce::juce_audio_utils
            juce::juce_audio_devices
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

# The keyboard, as an instrument: loads in Live's instrument slot.
okstudio_add_plugin(Keys
    PRODUCT_NAME  "Keys"
    PLUGIN_CODE   Keys
    BUNDLE_SUFFIX keys
    ${_keysCopy})
keys_configure_target(Keys)

# Experimental sibling: the same keyboard, classified as a MIDI effect (VST3 category
# Fx) instead of an instrument, meant to sit *before* an instrument in the device chain
# and drive it. NOTE: Ableton Live rejects this — it treats a third-party VST3 MIDI
# effect as an audio effect and refuses to place it before an instrument (Live reserves
# that slot for native + Max for Live devices; see docs/ABLETON_LIVE.md). It does work
# in DAWs that allow VST3 MIDI effects (Bitwig, Cubase, Reaper). Off by default so it
# doesn't clutter Live as a dead audio effect; enable with -DKEYS_BUILD_MIDI_EFFECT=ON.
option(KEYS_BUILD_MIDI_EFFECT "Build the MIDI-effect variant (Keys FX); Ableton rejects it, works in other DAWs" OFF)
if (KEYS_BUILD_MIDI_EFFECT)
    okstudio_add_plugin(KeysFX
        PRODUCT_NAME  "Keys FX"
        PLUGIN_CODE   KyFx
        BUNDLE_SUFFIX keysfx
        MIDI_EFFECT
        ${_keysCopy})
    keys_configure_target(KeysFX)
    # Drops the audio output bus and reports isMidiEffect() (see PluginProcessor).
    target_compile_definitions(KeysFX PUBLIC KEYS_MIDI_EFFECT=1)
endif()

# Keys Host: the keyboard plus one hosted instrument VST3 in the same window, on one
# track. Sidesteps Live's "no plugin MIDI effects before an instrument" rule because
# Live just sees one instrument that makes sound. Design: docs/KEYS_HOST_DESIGN.md.
option(KEYS_BUILD_HOST "Build Keys Host (hosts an instrument VST3 inside Keys)" ON)
if (KEYS_BUILD_HOST)
    okstudio_add_plugin(KeysHost
        PRODUCT_NAME  "Keys Host"
        PLUGIN_CODE   KyHo
        BUNDLE_SUFFIX keyshost
        ${_keysCopy})
    keys_configure_target(KeysHost)
    target_sources(KeysHost PRIVATE
        src/host/KeysHostProcessor.cpp
        src/host/KeysHostEditor.cpp)
    # KEYS_HOST gates createPluginFilter and the embedded editor's updater;
    # JUCE_PLUGINHOST_VST3 turns on JUCE's bundled VST3 hosting.
    target_compile_definitions(KeysHost PUBLIC
        KEYS_HOST=1
        JUCE_PLUGINHOST_VST3=1)
endif()

# The keys-mcp stdio bridge: a small process Claude Code (or any local MCP client)
# launches that discovers the live plugin instance's loopback server and speaks MCP
# stdio to it. One shim serves both products (Keys, Keys Host); it has no PRODUCT
# filter, so instance discovery just picks whichever is live (see docs/MCP.md).
option(KEYS_BUILD_MCP_SHIM "Build the keys-mcp stdio bridge exe" ON)
if (KEYS_BUILD_MCP_SHIM)
    okstudio_add_mcp_shim(keys-mcp)
endif()

# Unit tests (console, JUCE UnitTest). Not built by the plugin targets; build and run
# with: cmake --build build --target Keys_tests && ctest --test-dir build -C Release
enable_testing()
add_executable(Keys_tests tests/KeysTests.cpp tests/MarkovTests.cpp tests/ArpTests.cpp
                          tests/ChordSourceTests.cpp tests/StateTests.cpp tests/LayoutTests.cpp
                          tests/TakeTests.cpp tests/ChordLibraryTests.cpp)
target_include_directories(Keys_tests PRIVATE src)
# juce_audio_basics is for ArpEngine's MidiBuffer/MidiMessage; the kit does not
# re-export it.
# Keys_SharedCode is the plugin itself: the processor, its APVTS and the editor. StateTests
# and LayoutTests need real ones - a migration is only observable through setStateInformation,
# and a layout invariant only through a panel that has actually been resized. Everything else
# here stays header-only against juce_audio_basics, which is why that stays too.
#
# This link is what those two files cost, and it is worth naming why it was paid: the migration
# mechanism was dead for months and nothing noticed, because no test could reach an APVTS.
target_link_libraries(Keys_tests PRIVATE okstudio_kit juce::juce_audio_basics Keys)
add_test(NAME Keys_tests COMMAND Keys_tests)
