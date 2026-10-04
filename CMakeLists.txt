cmake_minimum_required(VERSION 3.22)
project(DreamShareLite VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
if(MSVC)
  # Static runtime: the .vst3 runs without installing the Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(DS_AUDIO_PASSTHROUGH
       "Add a stereo in/out that passes audio through untouched (only if your DAW refuses plugins with no audio I/O)" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.6
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(UNIX AND NOT APPLE)
  set(DS_CURL TRUE)    # JUCE's HTTP client uses libcurl on Linux
else()
  set(DS_CURL FALSE)   # WinINet on Windows, NSURLSession on macOS
endif()

juce_add_plugin(DreamShareLite
  COMPANY_NAME             "Dreamdaw"
  PLUGIN_MANUFACTURER_CODE Drmd
  PLUGIN_CODE              Dsl1
  FORMATS                  VST3
  PRODUCT_NAME             "DreamShare Lite"
  VST3_CATEGORIES          Tools
  IS_SYNTH                 FALSE
  NEEDS_MIDI_INPUT         FALSE
  NEEDS_MIDI_OUTPUT        FALSE
  IS_MIDI_EFFECT           FALSE
  NEEDS_CURL               ${DS_CURL}
  COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(DreamShareLite PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/DreamClient.cpp
  Source/FeedView.cpp)

target_compile_definitions(DreamShareLite PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(DS_CURL)
  target_compile_definitions(DreamShareLite PUBLIC JUCE_USE_CURL=1)
else()
  target_compile_definitions(DreamShareLite PUBLIC JUCE_USE_CURL=0)
endif()

if(DS_AUDIO_PASSTHROUGH)
  target_compile_definitions(DreamShareLite PUBLIC DS_AUDIO_PASSTHROUGH=1)
endif()

if(MSVC)
  target_compile_options(DreamShareLite PRIVATE /utf-8)
endif()

target_link_libraries(DreamShareLite
  PRIVATE
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
