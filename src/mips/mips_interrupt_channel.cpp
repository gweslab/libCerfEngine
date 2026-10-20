#include <cerfengine/mips/mips_interrupt_channel.h>
#include <cerfengine/calling_convention.h>

#include "../host_platform.h"

#include <cerfengine/engine.h>
#include <cerfengine/host_request_channel.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>

namespace cerfengine {

CERFENGINE_REGISTER(MipsInterruptChannel);

MipsInterruptChannel::~MipsInterruptChannel() {
    platform::CloseWakeEvent(platform::WakeEvent{idle_event_, signal_idle_});
}

void MipsInterruptChannel::OnReady() {
    cpu_state_      = engine_.Get<MipsCpu>().State();
    device_ip_mask_ = engine_.GetSetup().mips.device_ip_mask;
    standby_lines_  = kMipsCauseIpMask & ~device_ip_mask_;
    host_requests_  = &engine_.Get<HostRequestChannel>();
    engine_.AttachClock(clock_);

    const platform::WakeEvent wake = platform::CreateWakeEvent();
    idle_event_  = wake.handle;
    signal_idle_ = wake.signal;
    if (idle_event_ == nullptr) {
        engine_.Panic("MipsInterruptChannel: creating the idle wake event failed, error %u",
                      platform::LastErrorCode());
    }
}

void MipsInterruptChannel::SignalIdleWake() {
    signal_idle_(idle_event_);
}

void MipsInterruptChannel::SetExternalInterruptLevel(uint32_t ip_mask) {
    const uint32_t prev = external_ip_.exchange(ip_mask, std::memory_order_acq_rel);
    if (ip_mask != prev) RequestChainExit();
    if (ip_mask & ~prev) SignalIdleWake();
}

void MipsInterruptChannel::RequestDispatch(uint32_t bits) {
    const uint32_t prev = dispatch_request_.fetch_or(bits, std::memory_order_acq_rel);
    RequestChainExit();
    if (bits & ~prev) SignalIdleWake();
}

void MipsInterruptChannel::SetResetRequest(bool pending) {
    if (pending) dispatch_request_.fetch_or(kDispatchReset, std::memory_order_acq_rel);
    else         dispatch_request_.fetch_and(~kDispatchReset, std::memory_order_acq_rel);
}

uint32_t MipsInterruptChannel::TakeDispatchRequests() {
    return dispatch_request_.exchange(0u, std::memory_order_acq_rel);
}

void MipsInterruptChannel::SetHostExit(bool requested) {
    host_exit_.store(requested, std::memory_order_release);
    std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
    if (requested) {
        word.fetch_or(MipsChainExit::kHost, std::memory_order_acq_rel);
        SignalIdleWake();
    } else {
        word.fetch_and(~MipsChainExit::kHost, std::memory_order_acq_rel);
    }
}

bool MipsInterruptChannel::TakeHostRequest() {
    return (dispatch_request_.fetch_and(~kDispatchHostClock, std::memory_order_acq_rel) &
            kDispatchHostClock) != 0u;
}

void MipsInterruptChannel::RegisterSuspendListener(std::function<void()> fn) {
    suspend_listeners_.push_back(std::move(fn));
}

void MipsInterruptChannel::RunSuspendListeners() {
    for (auto& fn : suspend_listeners_) fn();
}

uint64_t MipsInterruptChannel::CyclesOutsideSuspend() {
    if (suspended_) return suspend_start_cycle_ - suspended_cycles_;
    return clock_->Cycles() - suspended_cycles_;
}

/* Standby ends on an interrupt request its IM bit does not mask (VR4121 UM 10.2-10.4); Suspend
   ends on the ICU's int_all (VR4131 UM 11.1 p187, VR4102 UM Table 15-3 text p326). */
bool MipsInterruptChannel::InterruptPending(uint32_t idle) const {
    const uint32_t level = external_ip_.load(std::memory_order_acquire) & device_ip_mask_;
    if (idle != MipsIdle::kStandby) return level != 0u;
    const uint32_t lines = level | (cpu_state_->cp0_cause & standby_lines_);
    return (lines & cpu_state_->cp0_status & kMipsCauseIpMask) != 0u;
}

bool MipsInterruptChannel::WaitForInterrupt(uint32_t idle) {
    const MipsCpuState& s = *cpu_state_;
    bool woke = false;
    for (;;) {
        if (ResetRequested() || s.deep_sleep || host_exit_.load(std::memory_order_acquire)) break;
        if (TakeHostRequest()) host_requests_->ServiceRequests();
        woke = InterruptPending(idle);
        if (woke) break;
        clock_->IdleStep(idle_event_);
    }
    clock_->ExitIdle();
    return woke;
}

void MipsInterruptChannel::OnCpuReset() {
    if (suspended_) CloseSuspendSpan();
}

/* VR4102 UM p.643: "When the STANDBY instruction finishes the WB stage ... the internal clocks
   will shut down, thus freezing the pipeline." */
void MipsInterruptChannel::EnterIdle(uint32_t next_pc) {
    MipsCpuState& s = *cpu_state_;
    if (s.branch_state != MipsBranch::kNone) MipsResolvePendingBranch(s, next_pc);
    else                                     s.pc = next_pc;
}

/* Suspend stops TClock and every internal peripheral clock but the RTC/ICU/PMU ones, while
   the timer/interrupt clocks and MasterOut run (VR4102 UM Table 15-3 p326, VR4121 UM
   8.4.1(3)). */
void MipsInterruptChannel::OpenSuspendSpan() {
    suspend_start_cycle_ = clock_->Cycles();
    suspended_           = true;
    RunSuspendListeners();
}

void MipsInterruptChannel::CloseSuspendSpan() {
    suspended_cycles_ += clock_->Cycles() - suspend_start_cycle_;
    suspended_         = false;
    RunSuspendListeners();
}

void CERFENGINE_FASTCALL MipsInterruptChannel::StandbyInsnHelper(MipsInterruptChannel* channel,
                                                                 uint32_t next_pc) {
    channel->EnterIdle(next_pc);
    if (!channel->WaitForInterrupt(MipsIdle::kStandby)) {
        channel->cpu_state_->idle_wait = MipsIdle::kStandby;
    }
}

void CERFENGINE_FASTCALL MipsInterruptChannel::SuspendInsnHelper(MipsInterruptChannel* channel,
                                                                 uint32_t next_pc) {
    channel->EnterIdle(next_pc);
    channel->OpenSuspendSpan();
    if (channel->WaitForInterrupt(MipsIdle::kSuspend)) {
        channel->CloseSuspendSpan();
    } else {
        channel->cpu_state_->idle_wait = MipsIdle::kSuspend;
    }
}

bool MipsInterruptChannel::ResumeIdle() {
    MipsCpuState& s = *cpu_state_;
    if (!WaitForInterrupt(s.idle_wait)) return false;
    if (s.idle_wait == MipsIdle::kSuspend) CloseSuspendSpan();
    s.idle_wait = MipsIdle::kNone;
    return true;
}

/* TMPR39xx-um §5.1 (p.231): Halt mode stalls the pipeline; "The Status register IntMask field has no
   effect on the return to Active mode"; §5.3 (p.234) Doze stops internal operations the same way. */
void MipsInterruptChannel::HaltCore() { IdleAfterInsn(MipsIdle::kHalt); }

void MipsInterruptChannel::IdleAfterInsn(uint32_t kind) {
    EnterIdle(cpu_state_->pc + 4u);
    cpu_state_->idle_wait = kind;
}

/* STOPCPU "will cause the clock to the CPU core to be disabled"; "The bit is cleared whenever an
   enabled interrupt is set" (TMPR3911 §12.3.1 p12-13). */
void MipsInterruptChannel::StopCpuAfterAccess() {
    if (cpu_state_->isa_mode != 0u) {
        engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                     "MipsInterruptChannel: STOPCPU from MIPS16 code at 0x%08X is not modeled",
                     cpu_state_->pc);
    }
    IdleAfterInsn(MipsIdle::kStopCpu);
}

void MipsInterruptChannel::OnCpuStateRestored() {
    suspended_        = false;
    suspended_cycles_ = 0;
    const MipsCpuState& s = *cpu_state_;
    if (s.idle_wait == MipsIdle::kSuspend) {
        suspend_start_cycle_ = clock_->Cycles();
        suspended_           = true;
    }
}

}
