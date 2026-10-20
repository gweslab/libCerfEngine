#pragma once

#include "../calling_convention.h"
#include <atomic>
#include <cstdint>

#include "../engine.h"
#include "cpu_state.h"

namespace cerfengine {

struct ArmIrContext;

class HostRequestChannel;

class ArmInterruptChannel : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;
    ~ArmInterruptChannel() override;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

    void SetInterruptPending();
    void ClearInterruptPending();
    void SetIdleWake(bool level);

    uint32_t Level() const { return irq_line_.load(std::memory_order_acquire); }

    struct IrqGate {
        uint32_t level;
        bool     raise;
    };

    IrqGate EvaluateGate() const {
        IrqGate gate;
        gate.level = Level();
        gate.raise = gate.level != 0u && cpu_state_->cpsr.bits.irq_disable == 0u &&
                     cpu_state_->deep_sleep == 0u;
        return gate;
    }

    /* DDI 0406C.c B1.9.10 (p. B1-1219): "If SCTLR.FI == 0, IRQ exception entry
       is precise to an instruction boundary." */
    bool BackOutForIrq(uint32_t guest_pc) {
        if (!EvaluateGate().raise) return false;
        cpu_state_->gprs[ArmGpr::kR15] = guest_pc;
        return true;
    }

    static uint32_t CERFENGINE_CDECL BackOutForIrqHelper(ArmInterruptChannel* channel,
                                                         uint32_t guest_pc);

    void Wake();

    void RequestHostService();
    bool TakeHostRequest();

    void EnterIdle();
    void ResumeIdle();
    bool LeaveAfterAccess(uint32_t next_pc) {
        const uint32_t exits = std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
                                   .load(std::memory_order_acquire);
        if ((exits & (kChainExitClock | kChainExitIdle)) == 0u) return false;
        StopAfterInsn(next_pc);
        return true;
    }

    void SetClockStopped(bool stopped) { clock_stopped_ = stopped; }
    void WaitForClock();

private:
    friend void ArmIrWfi(ArmIrContext& c);

    bool WaitForInterrupt();
    bool ExitPending();
    void StopAfterInsn(uint32_t next_pc);

    static uint32_t CERFENGINE_FASTCALL WfiHelper(ArmInterruptChannel* channel, uint32_t next_pc);

    bool                  clock_stopped_ = false;
    std::atomic<uint32_t> irq_line_{0};
    std::atomic<uint32_t> idle_wake_line_{0};
    std::atomic<uint32_t> host_request_{0};

    ArmCpuState*        cpu_state_     = nullptr;
    HostRequestChannel* host_requests_ = nullptr;
    void*               idle_event_    = nullptr;
    int (CERFENGINE_STDCALL* signal_idle_)(void* event) = nullptr;
    ClockHost*          clock_         = nullptr;
};

}
