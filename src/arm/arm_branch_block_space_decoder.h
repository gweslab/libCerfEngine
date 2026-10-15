#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-21 branch/block space (ARM DDI 0406C.c,
   p. A5-214). */
class ArmBranchBlockSpaceDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};

}
