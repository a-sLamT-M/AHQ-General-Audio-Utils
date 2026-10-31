#include "pcmStationProcessor.h"
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new PcmStationProcessor; }
