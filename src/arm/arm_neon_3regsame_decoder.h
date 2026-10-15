#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for A7.4.1 (3-reg same length). */
class ArmNeon3RegSameDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};

}
