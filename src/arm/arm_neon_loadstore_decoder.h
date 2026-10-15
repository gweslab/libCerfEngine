#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

class ArmNeonLoadStoreDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};

}
