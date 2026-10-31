set(effect_formats VST3 Standalone)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND effect_formats LV2)
endif()
juce_add_plugin(ahq_retro_tools
    VERSION 0.1.0 COMPANY_NAME "AHQ" BUNDLE_ID "audio.ahq.retrotools"
    PLUGIN_MANUFACTURER_CODE Ahqa PLUGIN_CODE Retr PRODUCT_NAME "AHQ Retro Tools"
    LV2URI "https://ahq.audio/plugins/retro-tools" FORMATS ${effect_formats}
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(ahq_retro_tools PRIVATE retroToolsProcessor.cpp retroToolsProcessor.h ../common/pluginState.cpp)
target_compile_features(ahq_retro_tools PRIVATE cxx_std_20)
target_compile_definitions(ahq_retro_tools PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_JACK=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ahq_retro_tools PRIVATE ahq::audio_core juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
