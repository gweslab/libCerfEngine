#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

class ThumbStackControlDecoder;
class ThumbTransferDecoder;
struct DecodedInsn;

class ThumbDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeThumb(DecodedInsn* insn, uint16_t op);

private:
    bool DecodeAddSubtract(DecodedInsn* insn, uint16_t op);
    bool DecodeAddToPcOrSp(DecodedInsn* insn, uint16_t op);
    bool DecodeAluOperations(DecodedInsn* insn, uint16_t op);
    bool DecodeBranchExchange(DecodedInsn* insn, uint16_t op);
    bool DecodeBranchLinkPrefix(DecodedInsn* insn, uint16_t op);
    bool DecodeBranchLinkSuffix(DecodedInsn* insn, uint16_t op);
    bool DecodeConditionalBranch(DecodedInsn* insn, uint16_t op);
    bool DecodeImmediateOperations(DecodedInsn* insn, uint16_t op);
    bool DecodeShiftByImmediate(DecodedInsn* insn, uint16_t op);
    bool DecodeShiftByRegister(DecodedInsn* insn, uint16_t op, uint32_t type);
    bool DecodeSpecialDataProcessing(DecodedInsn* insn, uint16_t op);
    bool DecodeUnconditionalBranch(DecodedInsn* insn, uint16_t op);

    const cerfengine::ArmFeatures*       processor_config_ = nullptr;
    ThumbStackControlDecoder* stack_control_    = nullptr;
    ThumbTransferDecoder*     transfer_decoder_ = nullptr;
};

}
