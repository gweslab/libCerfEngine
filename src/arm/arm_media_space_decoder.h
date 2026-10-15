#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-16 media space (ARM DDI 0406C.c, p. A5-209). */
class ArmMediaSpaceDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);

private:
    bool DecodeBitfield(DecodedInsn* insn, ArmOpcode op);
    bool DecodePackSatReverse(DecodedInsn* insn, ArmOpcode op);
    bool DecodeSignedSaturate(DecodedInsn* insn, ArmOpcode op);
    bool DecodeDualMultiply(DecodedInsn* insn, ArmOpcode op, bool is_long);
    bool DecodeMostSignificantMultiply(DecodedInsn* insn, ArmOpcode op);

    const cerfengine::ArmFeatures* processor_config_ = nullptr;
};

}
