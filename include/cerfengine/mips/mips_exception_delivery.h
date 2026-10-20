#pragma once

#include "../calling_convention.h"
#include <cstdint>

#include "../engine.h"
#include "mips_system_host.h"

namespace cerfengine {

struct MipsCpuState;

class MipsExceptionDelivery : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    void EnterException(uint32_t cause, bool refill_eligible);

    void SetMmuFaultRegs(uint32_t va);

    void RaiseTlbException(uint32_t va, MipsAccess acc, MipsTlbResult res);
    void DeliverFetchTlbException(uint32_t va, MipsTlbResult res);
    void DeliverFetchAddressError(uint32_t va);
    void RaiseAddressError(uint32_t va, MipsAccess acc);
    void RaiseOverflowException();
    void RaiseCoprocessorUnusable(uint32_t unit);
    [[noreturn]] void LeaveBlock();

    /* QEMU cpu_mips_hw_interrupts_enabled / _pending (MIPS internal.h). */
    bool InterruptReady() const;
    bool InterruptsEnabled() const {
        return hooks_.interrupts_enabled(hooks_.context, *cpu_state_);
    }

    void DeliverInterrupt();

    static void CERFENGINE_FASTCALL SyscallHelper(MipsExceptionDelivery* ex);
    static void CERFENGINE_FASTCALL BreakHelper(MipsExceptionDelivery* ex);
    static void CERFENGINE_FASTCALL Cop0UsableHelper(MipsExceptionDelivery* ex);
    static void CERFENGINE_FASTCALL CopUnusableHelper(MipsExceptionDelivery* ex, uint32_t unit);

    static void CERFENGINE_CDECL ArithOverflowHelper(MipsExceptionDelivery* ex, uint32_t pc);

private:
    [[noreturn]] void Unwind();

    MipsCpuState*                  cpu_state_ = nullptr;
    cerfengine::MipsExceptionHooks hooks_;
};

}
