#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/calling_convention.h>

#include "../host_platform.h"

#include <atomic>

#include <cerfengine/engine.h>
#include <cerfengine/host_request_channel.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmInterruptChannel);

ArmInterruptChannel::~ArmInterruptChannel() {
    platform::CloseWakeEvent(platform::WakeEvent{idle_event_, signal_idle_});
}

void ArmInterruptChannel::OnReady() {
    cpu_state_     = engine_.Get<ArmCpu>().State();
    host_requests_ = &engine_.Get<HostRequestChannel>();
    engine_.AttachClock(clock_);

    const platform::WakeEvent wake = platform::CreateWakeEvent();
    idle_event_  = wake.handle;
    signal_idle_ = wake.signal;
    if (idle_event_ == nullptr) {
        engine_.Panic("ArmInterruptChannel: creating the idle wake event failed, error %u",
                      platform::LastErrorCode());
    }
}

void ArmInterruptChannel::SetInterruptPending() {
    const bool edge = irq_line_.exchange(1u, std::memory_order_acq_rel) == 0u;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kChainExitIrq, std::memory_order_acq_rel);
    if (edge) {
        signal_idle_(idle_event_);
    }
}

void ArmInterruptChannel::ClearInterruptPending() {
    irq_line_.store(0u, std::memory_order_release);
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_and(~kChainExitIrq, std::memory_order_acq_rel);
}

void ArmInterruptChannel::SetIdleWake(bool level) {
    const uint32_t value = level ? 1u : 0u;
    const bool edge = idle_wake_line_.exchange(value, std::memory_order_acq_rel) == 0u &&
                      value != 0u;
    if (edge) {
        signal_idle_(idle_event_);
    }
}

void ArmInterruptChannel::Wake() {
    signal_idle_(idle_event_);
}

void ArmInterruptChannel::RequestHostService() {
    if (host_request_.exchange(1u, std::memory_order_acq_rel) == 0u) signal_idle_(idle_event_);
}

bool ArmInterruptChannel::TakeHostRequest() {
    return host_request_.load(std::memory_order_acquire) != 0u &&
           host_request_.exchange(0u, std::memory_order_acq_rel) != 0u;
}

uint32_t CERFENGINE_CDECL ArmInterruptChannel::BackOutForIrqHelper(
    ArmInterruptChannel* channel, uint32_t guest_pc) {
    return channel->BackOutForIrq(guest_pc) ? 1u : 0u;
}

/* ARM DDI 0406C.c B1.8.14: a WFI wake-up event is "a physical IRQ interrupt,
   regardless of the value of the CPSR.I bit". */
bool ArmInterruptChannel::WaitForInterrupt() {
    bool woke = false;
    while (!woke && !ExitPending()) {
        if (!clock_stopped_) {
            woke = irq_line_.load(std::memory_order_acquire) != 0u;
            /* SA-1110 Dev Man §9.5.2.2: with ICCR.DIM = 0 any enabled interrupt, masked
               or unmasked, ends idle mode; the WFI completes and execution resumes. */
            woke = woke || idle_wake_line_.load(std::memory_order_acquire) != 0u;
        }
        if (!woke) clock_->IdleStep(idle_event_);
    }
    clock_->ExitIdle();
    return woke;
}

void ArmInterruptChannel::EnterIdle() {
    cpu_state_->idle_wait = 1u;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kChainExitIdle, std::memory_order_acq_rel);
}

void ArmInterruptChannel::ResumeIdle() {
    if (!WaitForInterrupt()) return;
    cpu_state_->idle_wait = 0u;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_and(~kChainExitIdle, std::memory_order_acq_rel);
}

/* ARM DDI 0406C.c A2.5.2 (p. A2-52): "When an instruction in an IT block
   completes its execution normally, ITSTATE advances to the next line of
   Table A2-2." */
void ArmInterruptChannel::StopAfterInsn(uint32_t next_pc) {
    cpu_state_->gprs[ArmGpr::kR15] = next_pc;
    ArmItStoreToCpsr(*cpu_state_, ArmItAdvance(ArmItFromCpsr(*cpu_state_)));
}

uint32_t CERFENGINE_FASTCALL ArmInterruptChannel::WfiHelper(ArmInterruptChannel* channel,
                                                    uint32_t next_pc) {
    if (channel->WaitForInterrupt()) return 0u;
    channel->EnterIdle();
    channel->StopAfterInsn(next_pc);
    return 1u;
}

bool ArmInterruptChannel::ExitPending() {
    if (cpu_state_->reset_pending || cpu_state_->deep_sleep) return true;
    const uint32_t exits = std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
                               .load(std::memory_order_acquire);
    if ((exits & ~(kChainExitIrq | kChainExitFlush | kChainExitClock | kChainExitIdle)) != 0u) {
        return true;
    }
    if (TakeHostRequest()) host_requests_->ServiceRequests();
    return false;
}

void ArmInterruptChannel::WaitForClock() {
    while (clock_stopped_ && !ExitPending()) clock_->WaitStep(idle_event_);
}

}
