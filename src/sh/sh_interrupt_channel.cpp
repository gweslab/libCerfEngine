#include <cerfengine/sh/sh_interrupt_channel.h>

#include "../guest_idle_wait.h"

#include <cerfengine/host_request_channel.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_interrupt_host.h>
#include <cerfengine/sh/sh_sleep_host.h>

namespace cerfengine {

CERFENGINE_REGISTER(ShInterruptChannel);

void ShInterruptChannel::OnReady() {
    cpu_           = &engine_.Get<ShCpu>();
    cpu_state_     = cpu_->State();
    idle_          = &engine_.Get<GuestIdleWait>();
    host_requests_ = &engine_.Get<HostRequestChannel>();
    host_requests_->SetKick(
        [this] { idle_->SetExitBit(cpu_state_->chain_exit_request, kShChainExitHostRequest, true); });
}

void ShInterruptChannel::SetHost(ShInterruptHost* host) {
    host_ = host;
}

void ShInterruptChannel::SetSleepHost(ShSleepHost* host) {
    sleep_host_ = host;
}

void ShInterruptChannel::PublishLevel(uint32_t level) {
    const uint32_t prev = level_.exchange(level, std::memory_order_acq_rel);
    if (prev == level) return;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kShChainExitInterrupt, std::memory_order_acq_rel);
    if (level > prev) idle_->Signal();
}

void ShInterruptChannel::SetHostExit(bool requested) {
    idle_->SetExitBit(cpu_state_->chain_exit_request, kShChainExitHost, requested);
}

bool ShInterruptChannel::HostExitRequested() const {
    return (std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request).load(std::memory_order_acquire) &
            kShChainExitHost) != 0u;
}

void ShInterruptChannel::RequestReset() {
    idle_->SetExitBit(cpu_state_->chain_exit_request, kShChainExitReset, true);
}

bool ShInterruptChannel::ResetRequested() const {
    return (std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request).load(std::memory_order_acquire) &
            kShChainExitReset) != 0u;
}

void ShInterruptChannel::ClearResetRequest() {
    idle_->SetExitBit(cpu_state_->chain_exit_request, kShChainExitReset, false);
}

bool ShInterruptChannel::AcceptInterrupt(uint32_t sr) {
    if (!ShSrAcceptsInterrupt(sr, level_.load(std::memory_order_acquire))) return false;
    const uint32_t intevt = host_->AcceptInterrupt(sr);
    if (intevt == 0u) return false;
    cpu_->EnterInterrupt(intevt);
    return true;
}

void ShInterruptChannel::DeliverPending() {
    constexpr uint32_t        kDelivered = kShChainExitInterrupt | kShChainExitHostRequest;
    std::atomic_ref<uint32_t> chain_exit(cpu_state_->chain_exit_request);
    if ((chain_exit.load(std::memory_order_acquire) & kDelivered) != 0u) {
        const uint32_t taken = chain_exit.fetch_and(~kDelivered, std::memory_order_acq_rel);
        if ((taken & kShChainExitHostRequest) != 0u) host_requests_->RunListeners();
    }
    AcceptInterrupt(cpu_state_->sr);
}

void ShInterruptChannel::TakeHostRequest() {
    std::atomic_ref<uint32_t> chain_exit(cpu_state_->chain_exit_request);
    if ((chain_exit.fetch_and(~kShChainExitHostRequest, std::memory_order_acq_rel) & kShChainExitHostRequest) != 0u) {
        host_requests_->RunListeners();
    }
}

bool ShInterruptChannel::LevelEndsSleep(uint32_t level) const {
    return ShSrAcceptsInterrupt(cpu_state_->sr & ~kShSrBl, level);
}

bool ShInterruptChannel::StandbyWakePending() const {
    return LevelEndsSleep(host_->StandbyWakeLevel());
}

bool ShInterruptChannel::WakeOnInterrupt() {
    if (!AcceptInterrupt(cpu_state_->sr & ~kShSrBl)) return false;
    cpu_state_->sleeping = kShSleepNone;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_and(~kShChainExitSleep, std::memory_order_acq_rel);
    return true;
}

bool ShInterruptChannel::StayInStandby() {
    if (sleep_host_->StandbyExited()) {
        cpu_state_->sleeping = kShSleepMode;
        return false;
    }
    if (!HostExitRequested()) sleep_host_->EnterStandby();
    return true;
}

bool ShInterruptChannel::Sleep() {
    if (cpu_state_->sleeping == kShSleepRequested) {
        if (sleep_host_->StandbySelected()) {
            cpu_state_->sleeping = kShSleepStandby;
            sleep_host_->EnterStandby();
            return false;
        }
        cpu_state_->sleeping = kShSleepMode;
    }
    if (cpu_state_->sleeping == kShSleepStandby && StayInStandby()) return false;
    return idle_->Wait([this] {
        TakeHostRequest();
        if (ResetRequested()) return GuestIdleWait::Poll::kExit;
        if (WakeOnInterrupt()) return GuestIdleWait::Poll::kWoken;
        return HostExitRequested() ? GuestIdleWait::Poll::kExit : GuestIdleWait::Poll::kWait;
    });
}

}
