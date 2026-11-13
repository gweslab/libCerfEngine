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
    void        WriteSr(uint32_t value);
    void EnterInterrupt(uint32_t intevt);
    void RaiseException(uint32_t expevt, uint32_t vector_offset, uint32_t exception_pc);

    static void CERFENGINE_CDECL WriteSrHelper(ShCpu* cpu, uint32_t value);
    static void CERFENGINE_CDECL EnterSleepHelper(ShCpu* cpu);

private:
    void EnterHandler(uint32_t return_pc, uint32_t vector_offset);

    ShCpuState               state_{};
    uint32_t                 sr_writable_ = kShSrWritableSh3;
    std::atomic<ShResetKind> pending_reset_kind_{ShResetKind::kPowerOn};
};

}
