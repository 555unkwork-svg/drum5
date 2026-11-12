cmake_minimum_required(VERSION 3.24)

# ---- Datos del producto -------------------------------------------------------
set(PLUGIN_NAME      "drum5")
set(PLUGIN_COMPANY   "MiMarca")        # misma marca provisoria que sampler5
set(PLUGIN_MFR_CODE  "Mmrc")           # 4 caracteres, al menos una mayuscula
set(PLUGIN_CODE      "Drm5")           # 4 caracteres, exactamente una mayuscula
# -----------------------------------------------------------------------------

project(Drum5 VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # sin DLLs de runtime

# JUCE_DIR permite usar una copia local (para compilar sin bajarlo cada vez)
include(FetchContent)
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- Motor (sin interfaz): los mismos fuentes van al plugin y a las pruebas --
set(ENGINE_SOURCES Source/Engine/DrumEngine.cpp)

function(drum_configure_target target)
    target_sources(${target} PRIVATE ${ENGINE_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
        PUBLIC
            juce::juce_recommended_config_flags)
endfunction()

# ---- Plugin ------------------------------------------------------------------
juce_add_plugin(Drum5
    COMPANY_NAME "${PLUGIN_COMPANY}"
    PLUGIN_MANUFACTURER_CODE ${PLUGIN_MFR_CODE}
    PLUGIN_CODE ${PLUGIN_CODE}
    FORMATS VST3 Standalone
    PRODUCT_NAME "${PLUGIN_NAME}"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Drum
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(Drum5 PRIVATE
    Source/PluginProcessor.cpp
    Source/Look.cpp
    Source/PluginEditor.cpp)

juce_add_binary_data(DrumAssets SOURCES Assets/Sampler5Sans.ttf)
set_target_properties(DrumAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(Drum5 PRIVATE DrumAssets)

drum_configure_target(Drum5)
target_link_libraries(Drum5
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_lto_flags)

# ---- Pruebas -----------------------------------------------------------------
option(DRUM_BUILD_TESTS "Compilar las pruebas" ON)
if(DRUM_BUILD_TESTS)
    juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
    target_sources(EngineTests PRIVATE Tests/EngineTests.cpp)
    drum_configure_target(EngineTests)

    # el procesador real del plugin, sin DAW (estado guardado y carga de samples)
    add_executable(PluginTests Tests/PluginTests.cpp)
    target_include_directories(PluginTests PRIVATE Source ${juce_SOURCE_DIR}/modules ${JUCE_DIR}/modules)
    target_compile_definitions(PluginTests PRIVATE $<TARGET_PROPERTY:Drum5,COMPILE_DEFINITIONS>)
    target_link_libraries(PluginTests PRIVATE Drum5)

    enable_testing()
    add_test(NAME EngineTests COMMAND EngineTests)
    add_test(NAME PluginTests COMMAND PluginTests)
endif()
