#include <cerfengine/host_request_channel.h>

#include "guest_idle_wait.h"

#include <utility>

namespace cerfengine {

CERFENGINE_REGISTER(HostRequestChannel);

void HostRequestChannel::OnReady() { idle_ = &engine_.Get<GuestIdleWait>(); }

void HostRequestChannel::RegisterListener(std::function<void()> fn) {
    listeners_.Add(std::move(fn));
}

void HostRequestChannel::SetKick(std::function<void()> kick) { kick_ = std::move(kick); }

void HostRequestChannel::Request() {
    if (kick_) {
        kick_();
        return;
    }
    if (pending_.exchange(1u, std::memory_order_acq_rel) == 0u) idle_->Signal();
}

void HostRequestChannel::RunListeners() {
    listeners_.Call();
}

void HostRequestChannel::TakePending() {
    if (pending_.exchange(0u, std::memory_order_acq_rel) != 0u) RunListeners();
}

}
