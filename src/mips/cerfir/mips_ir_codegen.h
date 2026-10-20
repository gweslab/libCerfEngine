#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_system_host.h>

#include "../../cerfir/cerfir_block.h"
#include "../../cerfir/cerfir_guest_model.h"
#include "../../cerfir/cerfir_host_lowering.h"
#include <cerfengine/force_inline.h>
#include "../mips_block_context.h"

namespace cerfengine {

class MipsBlockCompiler;
struct MipsIrContext;

struct MipsIrBlockInput {
    MipsBlockContext*        ctx;
    MipsBlockCompiler*       compiler;
    bool                     pending_entry;
    const cerfir::BlockLink* link;
};

class MipsIrCodegen : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kMips; }
    void OnReady() override;

    size_t Generate(const MipsIrBlockInput& in, uint8_t* code, uint8_t* code_end);

    MipsSystemHost* System() const { return system_; }

private:
    void TranslateBlock(const MipsIrBlockInput& in);
    template <bool kTraced, bool kCop0Checked>
    void TranslateInsns(const MipsIrBlockInput& in, MipsIrContext& c);
    void TranslatePendingEntry(const MipsIrBlockInput& in, MipsIrContext& c, bool traced);
    void EmitNullifyGate(MipsIrContext& c);
    void EmitResolve(MipsIrContext& c, uint32_t fallthrough);
    void EmitLeanResolve(MipsIrContext& c, uint32_t fallthrough);
    void EmitDispatcherEnd(MipsIrContext& c, bool delay_slot);
    void ExitTo(MipsIrContext& c, uint32_t target, uint16_t slot);
    void ExitTaken(MipsIrContext& c);
    void ExitRegister(MipsIrContext& c);
    CERFENGINE_NOINLINE void EmitPcTrace(MipsIrContext& c, MipsBlockCompiler* compiler);
    CERFENGINE_NOINLINE void VerifyBlock(const MipsIrBlockInput& in);

    cerfir::Block           block_;
    cerfir::HostLowering    lowering_;
    cerfir::GuestModel      model_;
    cerfir::HostGuestTarget target_;
    EngineHost*             host_   = nullptr;
    MipsSystemHost*         system_ = nullptr;
    bool                    verify_ = false;
    bool                    wide_gprs_ = true;
};

}
