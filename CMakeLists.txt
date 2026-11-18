cmake_minimum_required(VERSION 3.22)
project(Anyvoc VERSION 0.16.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(third_party/JUCE)
juce_add_plugin(Anyvoc
    COMPANY_NAME "Anyvoc Audio"
    BUNDLE_ID "audio.anyvoc.synth"
    PLUGIN_MANUFACTURER_CODE Avoc
    PLUGIN_CODE Av01
    PRODUCT_NAME "Anyvoc"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    FORMATS VST3 Standalone)
juce_generate_juce_header(Anyvoc)
target_sources(Anyvoc PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/SynthEngine.cpp Source/WavetableDocument.cpp Source/PresetLibrary.cpp)
target_include_directories(Anyvoc PUBLIC Source)
target_compile_definitions(Anyvoc PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Anyvoc PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(Anyvoc PRIVATE /utf-8)
endif()
option(ANYVOC_BUILD_TESTS "Build offline DSP and state tests" ON)
if(ANYVOC_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(AnyvocEngineTests PRODUCT_NAME "Anyvoc Engine Tests")
    juce_generate_juce_header(AnyvocEngineTests)
    target_sources(AnyvocEngineTests PRIVATE Tests/EngineTests.cpp Source/SynthEngine.cpp)
    target_include_directories(AnyvocEngineTests PRIVATE Source)
    target_compile_definitions(AnyvocEngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AnyvocEngineTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME EngineTests COMMAND AnyvocEngineTests)
    juce_add_console_app(AnyvocProcessorTests PRODUCT_NAME "Anyvoc Processor Tests")
    juce_generate_juce_header(AnyvocProcessorTests)
    target_sources(AnyvocProcessorTests PRIVATE Tests/ProcessorTests.cpp)
    target_include_directories(AnyvocProcessorTests PRIVATE Source)
    target_compile_definitions(AnyvocProcessorTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AnyvocProcessorTests PRIVATE Anyvoc juce::juce_audio_utils juce::juce_dsp)
    if(MSVC)
        target_compile_options(AnyvocProcessorTests PRIVATE /utf-8)
    endif()
    add_test(NAME ProcessorTests COMMAND AnyvocProcessorTests)
    juce_add_console_app(AnyvocWavetableTests PRODUCT_NAME "Anyvoc Wavetable Tests")
    juce_generate_juce_header(AnyvocWavetableTests)
    target_sources(AnyvocWavetableTests PRIVATE Tests/WavetableTests.cpp Source/WavetableDocument.cpp)
    target_include_directories(AnyvocWavetableTests PRIVATE Source)
    target_compile_definitions(AnyvocWavetableTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AnyvocWavetableTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME WavetableTests COMMAND AnyvocWavetableTests)
    juce_add_console_app(AnyvocPresetLibraryTests PRODUCT_NAME "Anyvoc Preset Library Tests")
    juce_generate_juce_header(AnyvocPresetLibraryTests)
    target_sources(AnyvocPresetLibraryTests PRIVATE Tests/PresetLibraryTests.cpp Source/PresetLibrary.cpp)
    target_include_directories(AnyvocPresetLibraryTests PRIVATE Source)
    target_compile_definitions(AnyvocPresetLibraryTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AnyvocPresetLibraryTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
    if(MSVC)
        target_compile_options(AnyvocPresetLibraryTests PRIVATE /utf-8)
    endif()
    add_test(NAME PresetLibraryTests COMMAND AnyvocPresetLibraryTests)
endif()

# JUCE 8.0.12 omits Info.txt from its Windows resource-generation dependencies.
# Without this edge, an incremental version bump can retain the old FileVersion.
if(WIN32)
    foreach(resourceTarget IN ITEMS Anyvoc AnyvocEngineTests AnyvocProcessorTests AnyvocWavetableTests AnyvocPresetLibraryTests)
        if(TARGET ${resourceTarget}_rc_lib)
            get_target_property(resourceDirectory ${resourceTarget} JUCE_GENERATED_SOURCES_DIRECTORY)
            get_target_property(resourceInfo ${resourceTarget} JUCE_INFO_FILE)
            add_custom_command(OUTPUT "${resourceDirectory}/${resourceTarget}_resources.rc"
                APPEND DEPENDS "${resourceInfo}")
        endif()
    endforeach()
endif()
