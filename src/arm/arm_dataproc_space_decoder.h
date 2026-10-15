#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmMultiplySpaceDecoder;
struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-2 data-processing and miscellaneous space
   (ARM DDI 0406C.c, p. A5-196), including its Table A5-12 synchronization
   and Table A5-10/A5-11 extra load/store sub-spaces. */
class ArmDataprocSpaceDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);

private:
    bool DecodeDataProcessingImm(DecodedInsn* insn, ArmOpcode op);
    bool DecodeDataProcessingReg(DecodedInsn* insn, ArmOpcode op);
    bool DecodeDataProcessingShiftedReg(DecodedInsn* insn, ArmOpcode op);
    bool DecodeMiscSpace(DecodedInsn* insn, ArmOpcode op);
    bool DecodeSyncSpace(DecodedInsn* insn, ArmOpcode op);
    bool DecodeLdrexStrex(DecodedInsn* insn, ArmOpcode op);
    bool DecodeExtraLoadStore(DecodedInsn* insn, ArmOpcode op);
    bool DecodeMsrImmHints(DecodedInsn* insn, ArmOpcode op);

    ArmMultiplySpaceDecoder* multiply_decoder_ = nullptr;
    const cerfengine::ArmFeatures*      processor_config_ = nullptr;
};

}
