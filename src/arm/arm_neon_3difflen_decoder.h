#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the bit23==1 && c==0 sub-region of the cond==15 NEON
   data-processing encoding: 3-registers-of-different-lengths (A7.4.2). */
class ArmNeon3DiffLenDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};

}
