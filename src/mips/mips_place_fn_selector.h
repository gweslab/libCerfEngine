#pragma once

#include <cerfengine/engine.h>
#include "mips_decoded_insn.h"

namespace cerfengine {

class MipsPlaceFnSelector : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    MipsPlaceFn Select(const MipsDecodedInsn* d);
};

}
