#include "guest_idle_wait.h"

#include <atomic>

namespace cerfengine {

CERFENGINE_REGISTER(GuestIdleWait);

GuestIdleWait::~GuestIdleWait() {
    platform::CloseWakeEvent(platform::WakeEvent{event_, signal_});
}

void GuestIdleWait::OnReady() {
    engine_.AttachClock(clock_);
    const platform::WakeEvent wake = platform::CreateWakeEvent();
    event_  = wake.handle;
    signal_ = wake.signal;
    if (event_ == nullptr) {
        engine_.Panic("GuestIdleWait: creating the idle wake event failed, error %u", platform::LastErrorCode());
    }
}

void GuestIdleWait::SetExitBit(uint32_t& word, uint32_t bit, bool requested) {
    std::atomic_ref<uint32_t> exits(word);
    if (requested) {
        exits.fetch_or(bit, std::memory_order_acq_rel);
        Signal();
    } else {
        exits.fetch_and(~bit, std::memory_order_acq_rel);
    }
}

}
