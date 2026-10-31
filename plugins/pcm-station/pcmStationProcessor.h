#pragma once
#include "../common/synthProcessor.h"
class PcmStationProcessor final : public SynthProcessor {
  public:
    PcmStationProcessor() : SynthProcessor(true) {}
};
