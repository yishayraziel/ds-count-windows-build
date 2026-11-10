cmake_minimum_required(VERSION 3.22)
project(DSCount VERSION 2.0.0 LANGUAGES C CXX)

# Single place for the product identity. Change these two lines to rebrand.
set(DSCOUNT_PRODUCT_NAME "DS-COUNT")
set(DSCOUNT_COMPANY_NAME "Yishay Raziel")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
endif()

option(DSCOUNT_BUILD_TESTS "Build unit and integration tests" ON)

if(EXISTS "${CMAKE_SOURCE_DIR}/JUCE/CMakeLists.txt")
  add_subdirectory(JUCE)
elseif(DEFINED JUCE_DIR_OVERRIDE)
  add_subdirectory(${JUCE_DIR_OVERRIDE} ${CMAKE_BINARY_DIR}/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
  set(DSCOUNT_FORMATS VST3 AU Standalone)
else()
  set(DSCOUNT_FORMATS VST3 Standalone)
endif()

juce_add_plugin(DSCount
  COMPANY_NAME "${DSCOUNT_COMPANY_NAME}"
  COMPANY_WEBSITE "https://yraziel.com"
  BUNDLE_ID com.yraziel.dscount
  PLUGIN_MANUFACTURER_CODE Yraz
  PLUGIN_CODE Dsct
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "DS-COUNT Standalone processes the audio input you select."
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  FORMATS ${DSCOUNT_FORMATS}
  VST3_CATEGORIES Fx Dynamics Mastering
  AU_MAIN_TYPE kAudioUnitType_Effect
  PRODUCT_NAME "${DSCOUNT_PRODUCT_NAME}")

juce_add_binary_data(DSCountData SOURCES
  Assets/Fonts/DSCSystem-Bold.ttf
  Assets/Fonts/DSCSystem-Regular.ttf
  Assets/Fonts/ChicagoFLF.ttf)

target_sources(DSCount PRIVATE
  Source/DSP/Engine.cpp
  Source/DSP/Chain.cpp
  Source/Parameters/ParameterManager.cpp
  Source/Presets/FactoryPresets.cpp
  Source/Presets/PresetManager.cpp
  Source/Plugin/PluginProcessor.cpp
  Source/Plugin/PluginEditor.cpp
  Source/UI/Theme.cpp
  Source/UI/Crt.cpp
  Source/UI/Widgets.cpp
  Source/UI/Graphs.cpp
  Source/UI/Panels.cpp)

target_compile_definitions(DSCount PUBLIC
  _USE_MATH_DEFINES=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(DSCount
  PRIVATE DSCountData juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(DSCOUNT_BUILD_TESTS)
  enable_testing()
  file(GLOB DSCOUNT_UNIT_TESTS Tests/Unit/*.cpp)
  set(DSCOUNT_DSP_SOURCES Source/DSP/Engine.cpp Source/DSP/Chain.cpp)
  add_executable(dscount_tests ${DSCOUNT_UNIT_TESTS} ${DSCOUNT_DSP_SOURCES})
  add_test(NAME dsp_unit_tests COMMAND dscount_tests)

  add_executable(dscount_measure Tests/AudioMeasurements/measure.cpp Tests/AudioMeasurements/Registry.cpp ${DSCOUNT_DSP_SOURCES})

  juce_add_console_app(dscount_integration PRODUCT_NAME "dscount_integration")
  target_sources(dscount_integration PRIVATE Tests/Integration/Integration.cpp)
  target_compile_definitions(dscount_integration PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(dscount_integration PRIVATE DSCount DSCountData juce::juce_audio_utils juce::juce_dsp)
  add_test(NAME plugin_integration_tests COMMAND dscount_integration)
endif()
