set(instrument_formats VST3 Standalone)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND instrument_formats LV2)
endif()
juce_add_plugin(ahq_pcm_station
    VERSION 0.1.0 COMPANY_NAME "AHQ" BUNDLE_ID "audio.ahq.pcmstation"
    PLUGIN_MANUFACTURER_CODE Ahqa PLUGIN_CODE Pcst PRODUCT_NAME "AHQ PCM Station"
    LV2URI "https://ahq.audio/plugins/pcm-station" FORMATS ${instrument_formats}
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(ahq_pcm_station PRIVATE
    pcmStationProcessor.cpp pcmStationProcessor.h ../common/synthProcessor.cpp ../common/synthProcessor.h ../common/synthEditor.cpp ../common/synthEditor.h
    ../common/pluginState.cpp ../common/pluginAnalysis.cpp ../common/analysisDisplay.cpp)
target_compile_features(ahq_pcm_station PRIVATE cxx_std_20)
target_compile_definitions(ahq_pcm_station PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_JACK=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ahq_pcm_station PRIVATE
    ahq::audio_core juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
