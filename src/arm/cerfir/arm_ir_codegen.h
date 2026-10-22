#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/arm/arm_instruction_timing.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/engine.h>

#include "../../cerfir/cerfir_block.h"
#include "../../cerfir/cerfir_builder.h"
#include "../../cerfir/cerfir_guest_model.h"
#include "../../cerfir/cerfir_host_lowering.h"
#include <cerfengine/force_inline.h>

namespace cerfengine {

class ArmBlockCompiler;
class ArmIrMemorySlowPath;
struct ArmIrContext;

struct ArmIrBlockInput {
    BlockContext*               ctx;
    const ArmInstructionTiming* timing;
    bool                        uniform_timing;
    ArmBlockCompiler*           compiler;
    uint32_t                    tail_itstate;
    bool                        tail_itstate_valid;
    const cerfir::BlockLink*    link;
};

class ArmIrCodegen : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kArm; }
    void OnReady() override;

    size_t Generate(const ArmIrBlockInput& in, uint8_t* code, uint8_t* code_end);

    const void* EntryStub() const { return target_.entry_stub; }

private:
    void TranslateBlock(const ArmIrBlockInput& in);
    template <bool kUniform, bool kTraced>
    void TranslateInsns(const ArmIrBlockInput& in, cerfir::Builder& b, ArmIrContext& c);
    void EmitBlockEnd(const ArmIrBlockInput& in, ArmIrContext& c);
    cerfir::Builder::Guard EmitCondition(ArmIrContext& c, uint32_t cond);
    CERFENGINE_NOINLINE void EmitPcTrace(cerfir::Builder& b, const ArmIrBlockInput& in,
                                         uint32_t address);
    CERFENGINE_NOINLINE void VerifyBlock(const ArmIrBlockInput& in);

    void BuildGuestModel();

    cerfir::Block           block_;
    cerfir::HostLowering    lowering_;
    cerfir::GuestModel      model_;
    cerfir::HostSoftTlb     tlb_;
    cerfir::HostGuestTarget target_;
    ArmIrMemorySlowPath*    slow_   = nullptr;
    EngineHost*             host_   = nullptr;
    bool                    verify_ = false;
};

}
