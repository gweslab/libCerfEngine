#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

class NeonUnconditionalDecoder;
struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-23 cond == 1111 unconditional space
   (ARM DDI 0406C.c, p. A5-216). */
class ArmUnconditionalSpaceDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);

private:
    const cerfengine::ArmFeatures*       processor_config_           = nullptr;
    NeonUnconditionalDecoder* neon_unconditional_decoder_ = nullptr;
};

}
