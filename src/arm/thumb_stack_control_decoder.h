#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

struct DecodedInsn;

class ThumbStackControlDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeStackControlGroup(DecodedInsn* insn, uint16_t op);

private:
    bool DecodeAdjustStackPointer(DecodedInsn* insn, uint16_t op);
    bool DecodeCompareAndBranch(DecodedInsn* insn, uint16_t op);
    bool DecodeIfThen(DecodedInsn* insn, uint16_t op);
    bool DecodeHint(DecodedInsn* insn, uint32_t opa);
    bool DecodeChangeProcessorState(DecodedInsn* insn, uint16_t op);
    bool DecodeExtend(DecodedInsn* insn, uint16_t op);
    bool DecodeByteReverse(DecodedInsn* insn, uint16_t op);

    const cerfengine::ArmFeatures* processor_config_ = nullptr;
};

}
