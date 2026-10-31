set(curve_eq_formats VST3 Standalone)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND curve_eq_formats LV2)
endif()
juce_add_plugin(ahq_curve_eq
    VERSION 0.1.0 COMPANY_NAME "AHQ" BUNDLE_ID "audio.ahq.curveeq"
    PLUGIN_MANUFACTURER_CODE Ahqa PLUGIN_CODE Creq PRODUCT_NAME "AHQ Curve EQ"
    LV2URI "https://ahq.audio/plugins/curve-eq" FORMATS ${curve_eq_formats}
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(ahq_curve_eq PRIVATE
    curveEqProcessor.cpp curveEqProcessor.h curveEqEditor.cpp curveEqEditor.h
    ../common/pluginState.cpp ../common/pluginAnalysis.cpp ../common/analysisDisplay.cpp)
target_compile_features(ahq_curve_eq PRIVATE cxx_std_20)
target_compile_definitions(ahq_curve_eq PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_JACK=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ahq_curve_eq PRIVATE
    ahq::audio_core juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
