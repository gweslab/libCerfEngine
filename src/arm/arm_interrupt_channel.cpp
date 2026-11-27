#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/calling_convention.h>

#include "../guest_idle_wait.h"

#include <atomic>

#include <cerfengine/engine.h>
#include <cerfengine/host_request_channel.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/engine_setup.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmInterruptChannel);

void ArmInterruptChannel::OnReady() {
    ArmCpu& cpu    = engine_.Get<ArmCpu>();
    cpu_state_     = cpu.State();
    cpu.BindInterruptChannel(this);
    host_requests_ = &engine_.Get<HostRequestChannel>();
    idle_          = &engine_.Get<GuestIdleWait>();
    system_        = engine_.GetSetup().arm_system;
}

void ArmInterruptChannel::SetInterruptPending() {
    const bool edge = irq_line_.exchange(1u, std::memory_order_acq_rel) == 0u;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kChainExitIrq, std::memory_order_acq_rel);
    if (edge) {
        idle_->Signal();
    }
}

void ArmInterruptChannel::ClearInterruptPending() {
    irq_line_.store(0u, std::memory_order_release);
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_and(~kChainExitIrq, std::memory_order_acq_rel);
}

void ArmInterruptChannel::SetFiqPending() {
    const bool edge = fiq_line_.exchange(1u, std::memory_order_acq_rel) == 0u;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kChainExitFiq, std::memory_order_acq_rel);
    if (edge) {
        idle_->Signal();
    }
}

void ArmInterruptChannel::ClearFiqPending() {
    fiq_line_.store(0u, std::memory_order_release);
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_and(~kChainExitFiq, std::memory_order_acq_rel);
}

void ArmInterruptChannel::SetIdleWake(bool level) {
    const uint32_t value = level ? 1u : 0u;
    const bool edge = idle_wake_line_.exchange(value, std::memory_order_acq_rel) == 0u &&
                      value != 0u;
    if (edge) {
        idle_->Signal();
    }
}

void ArmInterruptChannel::Wake() {
    idle_->Signal();
}

void ArmInterruptChannel::SetHostExit(bool requested) {
    idle_->SetExitBit(cpu_state_->chain_exit_request, kChainExitHost, requested);
}

uint32_t CERFENGINE_CDECL ArmInterruptChannel::BackOutForIrqHelper(
    ArmInterruptChannel* channel, uint32_t guest_pc) {
    return channel->BackOutForIrq(guest_pc) ? 1u : 0u;
}

bool ArmInterruptChannel::WaitForInterrupt() {
    return idle_->Wait([this] {
        if (ExitPending()) return GuestIdleWait::Poll::kExit;
        if (clock_stopped_) return GuestIdleWait::Poll::kWait;
        const bool woke = irq_line_.load(std::memory_order_acquire) != 0u ||
                          fiq_line_.load(std::memory_order_acquire) != 0u ||
                          idle_wake_line_.load(std::memory_order_acquire) != 0u;
        return woke ? GuestIdleWait::Poll::kWoken : GuestIdleWait::Poll::kWait;
    });
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
    system_->ArmResumeAfterWfi();
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
    if (channel->WaitForInterrupt()) return channel->system_->ArmResumeAfterWfi() ? 1u : 0u;
    channel->EnterIdle();
    channel->StopAfterInsn(next_pc);
    return 1u;
}

bool ArmInterruptChannel::ExitPending() {
    if (cpu_state_->reset_pending || cpu_state_->deep_sleep) return true;
    const uint32_t exits = std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
                               .load(std::memory_order_acquire);
    if ((exits & ~(kChainExitIrq | kChainExitFiq | kChainExitPrivilege | kChainExitFlush |
                   kChainExitClock | kChainExitIdle | kChainExitScan)) != 0u) {
        return true;
    }
    host_requests_->ServicePending();
    return false;
}

void ArmInterruptChannel::WaitForClock() {
    idle_->Hold([this] { return clock_stopped_ && !ExitPending(); });
}

}
