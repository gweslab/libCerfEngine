#pragma once

#include "../calling_convention.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "../engine.h"

namespace cerfengine {

struct MipsCpuState;

class HostRequestChannel;

class MipsInterruptChannel : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;
    ~MipsInterruptChannel() override;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    static constexpr uint32_t kDispatchReset     = 1u << 0;
    static constexpr uint32_t kDispatchHostClock = 1u << 1;

    void SetExternalInterruptLevel(uint32_t ip_mask);
    void SignalIdleWake();

    void     RequestDispatch(uint32_t bits);
    void     SetResetRequest(bool pending);
    void     SetHostExit(bool requested);
    uint32_t TakeDispatchRequests();
    bool     TakeHostRequest();
    uint32_t DispatchRequests() const {
        return dispatch_request_.load(std::memory_order_acquire);
    }
    bool ResetRequested() const { return (DispatchRequests() & kDispatchReset) != 0u; }

    uint32_t Level() const {
        return external_ip_.load(std::memory_order_acquire);
    }
    uint32_t DeviceIpMask() const { return device_ip_mask_; }

    bool     Suspended() const { return suspended_; }
    uint64_t CyclesOutsideSuspend();
    void     RegisterSuspendListener(std::function<void()> fn);

    void OnCpuStateRestored();
    void OnCpuReset();
    bool ResumeIdle();
    void StopCpuAfterAccess();
    void HaltCore();

    static void CERFENGINE_FASTCALL StandbyInsnHelper(MipsInterruptChannel* channel, uint32_t next_pc);
    static void CERFENGINE_FASTCALL SuspendInsnHelper(MipsInterruptChannel* channel, uint32_t next_pc);

private:
    void RunSuspendListeners();

    bool WaitForInterrupt(uint32_t idle);
    bool InterruptPending(uint32_t idle) const;
    void EnterIdle(uint32_t next_pc);
    void IdleAfterInsn(uint32_t kind);
    void OpenSuspendSpan();
    void CloseSuspendSpan();

    uint32_t standby_lines_ = 0;

    bool                               suspended_           = false;
    uint64_t                           suspend_start_cycle_ = 0;
    uint64_t                           suspended_cycles_    = 0;
    std::vector<std::function<void()>> suspend_listeners_;

    std::atomic<uint32_t> external_ip_{0};
    std::atomic<uint32_t> dispatch_request_{0};
    std::atomic<bool>     host_exit_{false};
    uint32_t              device_ip_mask_ = 0;

    MipsCpuState*       cpu_state_     = nullptr;
    HostRequestChannel* host_requests_ = nullptr;
    void*               idle_event_    = nullptr;
    int (CERFENGINE_STDCALL* signal_idle_)(void* event) = nullptr;
    ClockHost*          clock_         = nullptr;
};

}
