#pragma once

#include <cstdint>

#include <cerfengine/clock_host.h>
#include <cerfengine/engine.h>

#include "host_platform.h"

namespace cerfengine {

class GuestIdleWait : public EngineService {
public:
    using EngineService::EngineService;
    ~GuestIdleWait() override;

    void OnReady() override;

    enum class Poll { kWait, kWoken, kExit };

    void Signal() { signal_(event_); }

    void SetExitBit(uint32_t& word, uint32_t bit, bool requested);

    template <class PollFn>
    bool Wait(PollFn poll) {
        Poll result = poll();
        while (result == Poll::kWait) {
            clock_->IdleStep(event_);
            result = poll();
        }
        clock_->ExitIdle();
        return result == Poll::kWoken;
    }

    template <class HeldFn>
    void Hold(HeldFn held) {
        while (held()) clock_->WaitStep(event_);
    }

private:
    void*                  event_  = nullptr;
    platform::WakeSignalFn signal_ = nullptr;
    ClockHost*             clock_  = nullptr;
};

}
