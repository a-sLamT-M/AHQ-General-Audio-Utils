set(effect_formats VST3 Standalone)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND effect_formats LV2)
endif()
juce_add_plugin(ahq_hybrid_reverb
    VERSION 0.1.0 COMPANY_NAME "AHQ" BUNDLE_ID "audio.ahq.hybridreverb"
    PLUGIN_MANUFACTURER_CODE Ahqa PLUGIN_CODE Hyvr PRODUCT_NAME "AHQ Hybrid Reverb"
    LV2URI "https://ahq.audio/plugins/hybrid-reverb" FORMATS ${effect_formats}
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(ahq_hybrid_reverb PRIVATE hybridReverbProcessor.cpp hybridReverbProcessor.h ../common/pluginState.cpp ../common/impulseBank.cpp)
target_compile_features(ahq_hybrid_reverb PRIVATE cxx_std_20)
target_compile_definitions(ahq_hybrid_reverb PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_JACK=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ahq_hybrid_reverb PRIVATE ahq::audio_core juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
