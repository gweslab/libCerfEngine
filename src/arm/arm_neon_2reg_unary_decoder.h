#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the bit23==1 && c==0 && bits[21:20]==11 sub-region of the
   cond==15 NEON data-processing encoding: A7.4.5 "Two registers,
   miscellaneous". */
class ArmNeon2RegUnaryDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};

}
