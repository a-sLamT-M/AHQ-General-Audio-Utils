set(orbit_formats VST3 Standalone)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND orbit_formats LV2)
endif()

juce_add_plugin(ahq_orbit
    VERSION 0.1.0
    COMPANY_NAME "AHQ"
    BUNDLE_ID "audio.ahq.orbit"
    PLUGIN_MANUFACTURER_CODE Ahqa
    PLUGIN_CODE Orbt
    PRODUCT_NAME "AHQ Orbit"
    LV2URI "https://ahq.audio/plugins/orbit"
    FORMATS ${orbit_formats}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ahq_orbit PRIVATE
    orbitProcessor.cpp orbitProcessor.h
    orbitEditor.cpp orbitEditor.h
    ../common/pluginState.cpp ../common/pluginAnalysis.cpp ../common/analysisDisplay.cpp)
target_compile_features(ahq_orbit PRIVATE cxx_std_20)
target_compile_definitions(ahq_orbit PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_JACK=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ahq_orbit PRIVATE
    ahq::audio_core juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
