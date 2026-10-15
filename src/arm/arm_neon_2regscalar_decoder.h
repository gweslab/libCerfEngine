#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the bit23==1 && c==0 && bit[6]==1 sub-region of the cond==15
   NEON data-processing encoding: 2-registers-and-a-scalar (A7.4.3). */
class ArmNeon2RegScalarDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};

}
