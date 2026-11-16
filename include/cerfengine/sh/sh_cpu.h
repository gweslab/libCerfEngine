#pragma once

#include "../calling_convention.h"
#include "../engine.h"
#include "sh_cpu_state.h"

#include <atomic>

namespace cerfengine {

class ShCpu : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kSh;
    }
    void OnReady() override;

    ShCpuState* State() { return &state_; }
    uint32_t    SrWritable() const { return sr_writable_; }

    void        ResetState(ShResetKind kind);
    void        ReportResetKind(ShResetKind kind);
    void        SetPendingResetKind(ShResetKind kind);
    ShResetKind TakePendingResetKind();
    ShResetKind PendingResetKind() const { return pending_reset_kind_.load(std::memory_order_acquire); }
    void        WriteSr(uint32_t value);
    bool        BlocksExceptions() const { return (state_.sr & kShSrBl) != 0u; }
    void EnterInterrupt(uint32_t intevt);
    void RaiseException(uint32_t expevt, uint32_t vector_offset, uint32_t exception_pc);
    [[noreturn]] void RaiseGeneralException(uint32_t expevt, uint32_t spc);
    void SettleRestartPoint();

    static void CERFENGINE_CDECL WriteSrHelper(ShCpu* cpu, uint32_t value);
    static void CERFENGINE_CDECL ReturnFromExceptionHelper(ShCpu* cpu, uint32_t ssr);
    static void CERFENGINE_CDECL EnterSleepHelper(ShCpu* cpu);
    static void CERFENGINE_CDECL TrapHelper(ShCpu* cpu, uint32_t return_pc, uint32_t tra);

private:
    void ResetCpuRegisters(uint32_t expevt);
    void EnterHandler(uint32_t return_pc, uint32_t vector_offset);

    ShCpuState               state_{};
    uint32_t                 sr_writable_ = kShSrWritableSh3;
    uint32_t                 rte_sr_      = 0;
    std::atomic<ShResetKind> pending_reset_kind_{ShResetKind::kPowerOn};
};

}
