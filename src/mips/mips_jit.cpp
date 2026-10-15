#include <cerfengine/mips/mips_jit.h>
#include <cerfengine/calling_convention.h>

#include <excpt.h>

#include <cerfengine/engine.h>
#include "../host_platform.h"
#include <cerfengine/host_request_channel.h>
#include "mips_block_compiler.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_exception_delivery.h>
#include <cerfengine/mips/mips_interrupt_channel.h>
#include "mips_memory_access.h"
#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/physical_map.h>
#include "mips_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER_AS(MipsJit, GuestEngine);

int MipsJit::DispatchFaultFilter(void* exception_pointers, uint32_t guest_pc) {
    platform::HostFault fault;
    const int disposition = platform::CaptureFault(exception_pointers, fault);
    if (fault.code == platform::kHaltUnwindCode || fault.code == platform::kGuestUnwindCode) {
        return disposition;
    }
    CERFENGINE_LOG(engine_, kCaution,
                   "MipsJit: host exception 0x%08X at host addr %p while running guest PC "
                   "0x%08X\n", fault.code, fault.address, guest_pc);
    engine_.Panic("MipsJit: host exception while running guest PC 0x%08X", guest_pc);
}

void MipsJit::OnReady() {
    cpu_           = &engine_.Get<MipsCpu>();
    cpu_state_     = cpu_->State();
    mmu_           = &engine_.Get<MipsMmu>();
    cache_         = &engine_.Get<MipsTranslationCache>();
    compiler_      = &engine_.Get<MipsBlockCompiler>();
    channel_       = &engine_.Get<MipsInterruptChannel>();
    mem_access_    = &engine_.Get<MipsMemoryAccess>();
    exceptions_    = &engine_.Get<MipsExceptionDelivery>();
    host_requests_ = &engine_.Get<HostRequestChannel>();
    map_           = &engine_.Get<PhysicalMap>();
    host_          = &engine_.Host();
}

bool MipsJit::GuestIrqMasked() const {
    return !exceptions_->InterruptsEnabled();
}

__declspec(naked) void CERFENGINE_CDECL MipsJit::Dispatch(void*, MipsCpuState*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov  ecx, [esp + 20]
        mov  esi, [esp + 24]
        call ecx
        pop  edi
        pop  esi
        pop  ebx
        pop  ebp
        ret
    }
}

void MipsJit::PrintFatalDump() {
    const auto& g = cpu_state_->gpr;
    CERFENGINE_LOG(engine_, kCaution, "      guest PC=0x%08X  hi=0x%016llX  lo=0x%016llX\n",
                   cpu_state_->pc,
                   static_cast<unsigned long long>(cpu_state_->hi),
                   static_cast<unsigned long long>(cpu_state_->lo));
    for (uint32_t i = 0; i < kMipsNumGpr; i += 4) {
        CERFENGINE_LOG(engine_, kCaution, "      r%-2u=0x%016llX  r%-2u=0x%016llX  "
                       "r%-2u=0x%016llX  r%-2u=0x%016llX\n",
                       i,     static_cast<unsigned long long>(g[i]),
                       i + 1, static_cast<unsigned long long>(g[i + 1]),
                       i + 2, static_cast<unsigned long long>(g[i + 2]),
                       i + 3, static_cast<unsigned long long>(g[i + 3]));
    }
}

bool MipsJit::ResetPending() const { return channel_->ResetRequested(); }

void MipsJit::SetHostChainExit(bool requested) { channel_->SetHostExit(requested); }

GuestEngine::RunStatus MipsJit::Run() {
    if (channel_->DispatchRequests() != 0u) {
        const uint32_t req   = channel_->TakeDispatchRequests();
        const bool     reset = (req & MipsInterruptChannel::kDispatchReset) != 0u;
        if (reset) DeliverReset();
        if (req & MipsInterruptChannel::kDispatchHostClock) host_requests_->ServiceRequests();
        if (reset) return RunStatus::kRunning;
    }

    if (cpu_state_->idle_wait != MipsIdle::kNone && !channel_->ResumeIdle()) {
        return RunStatus::kRunning;
    }

    mem_access_->DeliverDueClockEvents();

    const uint32_t device_mask = channel_->DeviceIpMask();
    cpu_state_->cp0_cause = (cpu_state_->cp0_cause & ~device_mask) |
                            (channel_->Level() & device_mask);

    if (cpu_state_->branch_state == MipsBranch::kNone &&
        exceptions_->InterruptReady()) {
        exceptions_->DeliverInterrupt();
    }

    const uint32_t pc     = cpu_state_->pc;
    void*          native = compiler_->FindBlockNativeStart(pc);
    if (!native) {
        void* compiled = nullptr;
        auto  compile  = [this, pc, &compiled] { compiled = compiler_->Compile(pc); };
        if (!platform::CallHaltGuarded(compile)) return RunStatus::kHalted;
        native = compiled;
        if (!native) return RunStatus::kRunning;
    }

    __try {
        Dispatch(native, cpu_state_);
    } __except (DispatchFaultFilter(_exception_info(), pc)) {
        if (_exception_code() == platform::kHaltUnwindCode) return RunStatus::kHalted;
    }
    return RunStatus::kRunning;
}

void MipsJit::DeliverReset() {
    host_->OnResetDelivered();
    channel_->OnCpuReset();
    cpu_->ResetState();
    cache_->ContextSwitchFlush();
}

void MipsJit::SetExternalInterruptLevel(uint32_t ip_mask) {
    channel_->SetExternalInterruptLevel(ip_mask);
}

void MipsJit::SetFixedWindow(uint32_t va, uint32_t pa, uint32_t size) {
    mmu_->SetFixedWindow(va, pa, size);
    cache_->SetFixedWindowHost(pa, size);
}

void MipsJit::PrepareStateSave() {
    cpu_state_->reset_pending = channel_->ResetRequested() ? 1u : 0u;
}

void MipsJit::OnStateRestored() {
    channel_->SetResetRequest(cpu_state_->reset_pending != 0u);
    channel_->OnCpuStateRestored();
}

void MipsJit::SetResetPending(bool is_resume) {
    host_->OnResetRequested(is_resume);
    channel_->RequestDispatch(MipsInterruptChannel::kDispatchReset);
    if (is_resume) return;
    host_->OnRebootRequested();
}

void MipsJit::EnterDeepSleep() { cpu_state_->deep_sleep = 1; }

void MipsJit::ExitDeepSleep() {
    cpu_state_->deep_sleep = 0;
    channel_->SignalIdleWake();
}

void MipsJit::EnterIdleWait() { channel_->StopCpuAfterAccess(); }

void MipsJit::SetCoreClockStopped(bool stopped) {
    engine_.Panic("MipsJit: a core clock %s request is not modeled", stopped ? "stop" : "restart");
}

void MipsJit::FlushTranslationCache() { cache_->Flush(); }

void MipsJit::SetDmaRegion(uint32_t pa, uint32_t size) {
    cache_->AddDmaRegion(pa, size);
}

std::optional<uint8_t*> MipsJit::PeekGuestVa(uint32_t va) {
    uint32_t pa = 0;
    if (mmu_->Translate(cpu_state_, va, MipsAccess::kRead, &pa) !=
        MipsTlbResult::kMatch) {
        return std::nullopt;
    }
    uint8_t* host = map_->PeekRead(pa);
    if (!host) return std::nullopt;
    return host;
}

uint8_t* MipsJit::ResolveGuestVaToHost(uint32_t va) {
    uint32_t pa = 0;
    if (mmu_->Translate(cpu_state_, va, MipsAccess::kRead, &pa) !=
        MipsTlbResult::kMatch) {
        return nullptr;
    }
    uint8_t* w = map_->PeekWrite(pa);
    return w ? w : map_->PeekRead(pa);
}

bool MipsJit::ResolveGuestVaToPa(uint32_t va, uint32_t* pa) {
    return mmu_->Translate(cpu_state_, va, MipsAccess::kRead, pa) ==
           MipsTlbResult::kMatch;
}

}
