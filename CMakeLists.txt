cmake_minimum_required(VERSION 3.22)
project(UBERVYSEKATEL VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

# JUCE fills the Unicode VST3 class name byte by byte from JucePlugin_Name, so a Cyrillic name
# would arrive in the host as mojibake: write the real name (Source/UbvName.h) there instead.
include(cmake/patch_juce.cmake)
ubv_patch_juce("${juce_SOURCE_DIR}")

# Display name: Cyrillic where the toolchain is UTF-8 throughout (macOS: AU name in Info.plist);
# on Windows an ASCII JucePlugin_Name, the VST3 Unicode name comes from the patch above.
if(APPLE)
    set(UBV_PLUGIN_NAME "УБЕРВЫСЕКАТЕЛЬ")
else()
    set(UBV_PLUGIN_NAME "UBERVYSEKATEL")
endif()

juce_add_plugin(Ubervysekatel
    COMPANY_NAME "Insinistra"
    BUNDLE_ID com.insinistra.ubervysekatel
    PLUGIN_MANUFACTURER_CODE Insi
    PLUGIN_CODE Ubvk
    FORMATS VST3 AU
    PRODUCT_NAME "Ubervysekatel"
    PLUGIN_NAME "${UBV_PLUGIN_NAME}"
    DESCRIPTION "Timing against the host grid"
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_EXPORT_PREFIX UbervysekatelAU   # the default derives from PLUGIN_NAME: Cyrillic is no C++ token
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_SYNTH FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Ubervysekatel PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# PUBLIC: the format targets (Ubervysekatel_VST3, _AU) compile the wrappers and need UbvName.h
target_include_directories(Ubervysekatel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(Ubervysekatel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(MSVC)
    target_compile_options(Ubervysekatel PRIVATE /utf-8)
endif()

target_link_libraries(Ubervysekatel
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
