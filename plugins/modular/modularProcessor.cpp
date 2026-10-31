#include "modularProcessor.h"
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new ModularProcessor; }
