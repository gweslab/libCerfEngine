#pragma once

#include <cstdint>
#include <optional>

#include "../engine.h"
#include "../guest_engine.h"
#include "cpu_state.h"

namespace cerfengine {

class ArmBlockCompiler;
class ArmCpu;
class ArmInterruptChannel;
class ArmMmu;
class ArmTranslationCache;
class ArmTranslator;
class HostRequestChannel;

class ArmJit : public GuestEngine {
public:
    using GuestEngine::GuestEngine;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

    ArmCpuState* CpuState() { return cpu_state_; }

    void SetInterruptPending();
    void ClearInterruptPending();
    void SetIdleWake(bool level);

    RunStatus Run() override;
    bool      DeepSleep()    const override { return cpu_state_->deep_sleep != 0; }
    bool      ResetPending() const override { return cpu_state_->reset_pending != 0; }

    bool GuestIrqMasked() const override {
        return cpu_state_->cpsr.bits.irq_disable != 0u;
    }
    uint32_t Pc()           const override { return cpu_state_->gprs[ArmGpr::kR15]; }
    void     PrintFatalDump() override;

    std::optional<uint8_t*> PeekGuestVa(uint32_t va) override;
    uint8_t* ResolveGuestVaToHost(uint32_t va) override;
    bool     ResolveGuestVaToPa(uint32_t va, uint32_t* pa) override;

    void FlushTranslationCache() override;
    void SetResetPending(bool is_resume) override;
    void SetHostChainExit(bool requested) override;
    void EnterDeepSleep() override;
    void ExitDeepSleep() override;
    void EnterIdleWait() override;
    void SetCoreClockStopped(bool stopped) override;
    void SetDmaRegion(uint32_t pa, uint32_t size) override;

private:
    int  DispatchFaultFilter(void* exception_pointers, uint32_t guest_pc);
    [[noreturn]] void ReportHostFault(uint32_t code, const void* address, const char* registers,
                                      uint32_t guest_pc);
    void LogEmittedBytesAroundFault(uintptr_t fault);

    ArmCpuState* cpu_state_ = nullptr;

    ArmCpu*                 cpu_      = nullptr;
    ArmMmu*                 mmu_      = nullptr;
    ArmTranslator*          translator_ = nullptr;
    uint32_t                predecessor_va_ = 0;
    ArmTranslationCache*    cache_    = nullptr;
    ArmBlockCompiler*       compiler_ = nullptr;
    ArmInterruptChannel*    channel_  = nullptr;
    HostRequestChannel*     host_requests_ = nullptr;
    cerfengine::EngineHost* host_     = nullptr;
    ClockHost*              clock_    = nullptr;
    const void*             entry_stub_ = nullptr;
};

}
