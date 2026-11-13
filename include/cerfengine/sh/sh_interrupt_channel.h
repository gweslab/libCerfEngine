#pragma once

#include <atomic>
#include <cstdint>

#include "../engine.h"

namespace cerfengine {

class GuestIdleWait;
class HostRequestChannel;
class ShCpu;
class ShInterruptHost;
class ShSleepHost;
struct ShCpuState;

class ShInterruptChannel : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kSh;
    }
    void OnReady() override;

    void SetHost(ShInterruptHost* host);
    void SetSleepHost(ShSleepHost* host);
    void PublishLevel(uint32_t level);
    void SetHostExit(bool requested);
    void RequestReset();
    bool ResetRequested() const;
    void ClearResetRequest();
    void DeliverPending();
    bool Sleep();
    bool LevelEndsSleep(uint32_t level) const;
    bool StandbyWakePending() const;

private:
    bool AcceptInterrupt(uint32_t sr);
    void TakeHostRequest();
    bool WakeOnInterrupt();
    bool StayInStandby();
    bool HostExitRequested() const;

    std::atomic<uint32_t> level_{0};
    ShInterruptHost*      host_          = nullptr;
    ShSleepHost*          sleep_host_    = nullptr;
    ShCpu*                cpu_           = nullptr;
    ShCpuState*           cpu_state_     = nullptr;
    GuestIdleWait*        idle_          = nullptr;
    HostRequestChannel*   host_requests_ = nullptr;
};

}
