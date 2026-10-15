#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-22 coprocessor instructions and Supervisor
   Call space (ARM DDI 0406C.c, p. A5-215). */
class ArmCoprocSpaceDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);

private:
    const cerfengine::ArmFeatures* processor_config_ = nullptr;
};

}
