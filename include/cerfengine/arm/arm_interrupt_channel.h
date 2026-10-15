#pragma once

#include "../calling_convention.h"
#include <atomic>
#include <cstdint>

#include "../engine.h"

namespace cerfengine {

struct ArmCpuState;
struct BlockContext;
struct DecodedInsn;

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

    IrqGate EvaluateGate() const;

    /* DDI 0406C.c B1.9.10 (p. B1-1219): "If SCTLR.FI == 0, IRQ exception entry
       is precise to an instruction boundary." */
    bool BackOutForIrq(uint32_t guest_pc);

    static uint32_t CERFENGINE_CDECL BackOutForIrqHelper(ArmInterruptChannel* channel,
                                                         uint32_t guest_pc);

    void Wake();

    void RequestHostService();
    bool TakeHostRequest();

    void EnterIdle();
    void ResumeIdle();
    bool LeaveAfterAccess(uint32_t next_pc);

    void SetClockStopped(bool stopped) { clock_stopped_ = stopped; }
    void WaitForClock();

private:
    friend uint8_t* PlaceWfi(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx);
    friend uint8_t* EmitStopAfterIoAccess(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx);

    bool WaitForInterrupt();
    bool ExitPending();
    void StopAfterInsn(uint32_t next_pc);

    static uint32_t CERFENGINE_FASTCALL WfiHelper(ArmInterruptChannel* channel, uint32_t next_pc);
    static void CERFENGINE_FASTCALL StopAfterInsnHelper(ArmInterruptChannel* channel, uint32_t next_pc);

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
