#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine.h>

#include "../../cerfir/cerfir_block.h"
#include "../../cerfir/cerfir_guest_model.h"
#include "../../cerfir/cerfir_host_lowering.h"
#include "../sh_decoded_insn.h"

namespace cerfengine {

class EngineHost;
class ShCpu;
class ShMmu;
struct ShCpuState;

namespace cerfir {
class Builder;
}

class ShIrCodegen : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    size_t Generate(const ShDecodedInsn* insns, uint32_t count, const cerfir::BlockLink* link,
                    uint8_t* code, uint8_t* code_end);

private:
    using PcHelper = void(CERFENGINE_CDECL*)(ShIrCodegen* codegen, uint32_t guest_pc);

    void TranslateBlock(const ShDecodedInsn* insns, uint32_t count);
    void EmitPcHelperCall(cerfir::Builder& b, PcHelper helper, uint32_t guest_pc);
    [[noreturn]] void RaiseIllegalInstruction(uint32_t expevt, uint32_t exception_pc);

    static void CERFENGINE_CDECL TraceDispatchPcHelper(ShIrCodegen* codegen, uint32_t guest_pc);

    template <uint32_t kExpevt>
    static void CERFENGINE_CDECL IllegalInstructionHelper(ShIrCodegen* codegen, uint32_t exception_pc);
    template <uint32_t kExpevt>
    static void CERFENGINE_CDECL DspModeCheckHelper(ShIrCodegen* codegen, uint32_t exception_pc);
    template <uint32_t kExpevt>
    static void CERFENGINE_CDECL PrivilegedModeCheckHelper(ShIrCodegen* codegen, uint32_t exception_pc);

    EngineHost*             host_      = nullptr;
    ShCpu*                  cpu_       = nullptr;
    ShMmu*                  mmu_       = nullptr;
    const ShCpuState*       cpu_state_ = nullptr;
    cerfir::Block           block_;
    cerfir::HostLowering    lowering_;
    cerfir::GuestModel      model_;
    cerfir::HostGuestTarget target_;
    bool                    verify_ = false;
};

}
