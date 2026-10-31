#pragma once
#include "../common/synthProcessor.h"
class ModularProcessor final : public SynthProcessor {
  public:
    ModularProcessor() : SynthProcessor(false) {}
};
