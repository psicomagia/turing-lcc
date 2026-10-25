cmake_minimum_required(VERSION 3.22)
project(TuringLCC VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()

# Versão VST3/AU (Reaper e outros DAWs). A versão MPC (VST2 armhf) é compilada por mpc/build.sh.
add_subdirectory(JUCE)

# núcleo em C (o mesmo da versão MPC), separado das flags de aviso do JUCE
add_library(tmcore STATIC core/tm.c)
target_include_directories(tmcore PUBLIC core)
set_target_properties(tmcore PROPERTIES POSITION_INDEPENDENT_CODE ON C_EXTENSIONS ON)
find_package(Threads REQUIRED)
target_link_libraries(tmcore PUBLIC Threads::Threads)
if(UNIX)
    target_link_libraries(tmcore PUBLIC m)
endif()

set(TM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TM_FORMATS AU)
endif()

juce_add_plugin(TuringLCC
    COMPANY_NAME "Alex Sheeny"
    BUNDLE_ID "com.alexsheeny.turinglcc"
    PLUGIN_MANUFACTURER_CODE Alxs
    PLUGIN_CODE Tlcc
    PRODUCT_NAME "Turing LCC"
    FORMATS ${TM_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    NEEDS_WEB_BROWSER TRUE
    NEEDS_CURL FALSE)

juce_add_binary_data(TmWeb SOURCES web/tm.html)
target_sources(TuringLCC PRIVATE plugin/PluginProcessor.cpp plugin/PluginEditor.cpp)
target_compile_definitions(TuringLCC PUBLIC JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(TuringLCC
    PRIVATE TmWeb tmcore juce::juce_audio_utils juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
