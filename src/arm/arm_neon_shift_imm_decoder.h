#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the bit23==1 sub-region of the cond==15 NEON data-processing
   encoding: 2-reg-shift-immediate (A7.4.4) and 1-reg-modified-immediate
   (A7.4.6 - the L:imm6 == 0000xxx carve-out). */
class ArmNeonShiftImmDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};

}
