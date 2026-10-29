#include <cerfengine/arm/arm_jit.h>

#if CERFENGINE_BACKEND_X86_WIN32
#include <excpt.h>
#endif

#include <atomic>
#include <cstdio>

#include <cerfengine/engine.h>
#include "../guest_unwind.h"
#include "../host_platform.h"
#include <cerfengine/host_request_channel.h>
#include "../cerfir/cerfir_host_lowering.h"
#include "../jit_code_arena.h"
#include "arm_block_compiler.h"
#include "cerfir/arm_ir_codegen.h"
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include "arm_translation_cache.h"
#include <cerfengine/arm/arm_translator.h>

namespace cerfengine {

CERFENGINE_REGISTER_AS(ArmJit, GuestEngine);

namespace {

constexpr uintptr_t kFaultWindowBytes = 16;

#if CERFENGINE_GUARDED_DISPATCH
struct ArmBlockEntry {
    void*       native;
    void*       state;
    void*       mmu_state;
    const void* entry_stub;
};

void EnterArmBlock(void* context) {
    const ArmBlockEntry& entry = *static_cast<const ArmBlockEntry*>(context);
    cerfir::HostEnterBlock(entry.native, entry.state, entry.mmu_state, entry.entry_stub);
}
#endif

}

void ArmJit::LogEmittedBytesAroundFault(uintptr_t fault) {
    const JitCodeArena& arena = cache_->Arena();
    const uintptr_t base = reinterpret_cast<uintptr_t>(arena.RegionBase());
    const uintptr_t size = static_cast<uintptr_t>(arena.MaxSize());
    if (size == 0 || fault < base || fault >= base + size) {
        return;
    }

    const uintptr_t lo = (fault - base >= kFaultWindowBytes)
                             ? fault - kFaultWindowBytes : base;
    const uintptr_t hi = ((base + size) - fault > kFaultWindowBytes)
                             ? fault + kFaultWindowBytes : base + size;

    char line[128];
    int  n = 0;
    for (uintptr_t p = lo; p < hi; ++p) {
        const int written =
            std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), "%02X ",
                          *reinterpret_cast<const uint8_t*>(p));
        if (written <= 0 || written >= static_cast<int>(sizeof(line)) - n) break;
        n += written;
    }
    CERFENGINE_LOG(engine_, kCaution, "ArmJit:   emitted 0x%08lX..0x%08lX (fault at +0x%lX): %s\n",
                   static_cast<unsigned long>(lo), static_cast<unsigned long>(hi),
                   static_cast<unsigned long>(fault - lo), line);
}

void ArmJit::ReportHostFault(uint32_t code, const void* address, const char* registers,
                             uint32_t guest_pc) {
    CERFENGINE_LOG(engine_, kCaution, "ArmJit: host exception 0x%08X at host address %p while "
                   "running guest PC 0x%08X (CPSR=0x%08X)\n",
                   code, address, guest_pc, ArmPackCpsr(*cpu_state_));

    char symbol[512];
    if (host_->Symbolize(address, symbol, sizeof(symbol))) {
        CERFENGINE_LOG(engine_, kCaution, "ArmJit:   host symbol %s\n", symbol);
    }

    CERFENGINE_LOG(engine_, kCaution, "ArmJit:   %s\n", registers);

    LogEmittedBytesAroundFault(reinterpret_cast<uintptr_t>(address));

    const uint32_t* r = cpu_state_->gprs;
    for (uint32_t i = 0; i < 16u; i += 4u) {
        CERFENGINE_LOG(engine_, kCaution, "ArmJit:   r%u=0x%08X r%u=0x%08X r%u=0x%08X r%u=0x%08X\n",
                       i, r[i], i + 1u, r[i + 1u], i + 2u, r[i + 2u], i + 3u, r[i + 3u]);
    }
    if (entry_stub_ != nullptr) {
        char map[160];
        int  n = 0;
        for (uint32_t i = 0; i < 16u && n >= 0 && n < static_cast<int>(sizeof(map)); ++i) {
            n += std::snprintf(map + n, sizeof(map) - static_cast<size_t>(n), " r%u=x%u", i,
                               static_cast<unsigned>(cerfir::HostLowering::PinnedHostRegister(i)));
        }
        CERFENGINE_LOG(engine_, kCaution, "ArmJit:   pinned in host registers:%s\n", map);
    }
    engine_.Panic("ArmJit: host exception while running guest PC 0x%08X", guest_pc);
}

void ArmJit::OnReady() {
    cpu_        = &engine_.Get<ArmCpu>();
    cpu_state_  = cpu_->State();
    mmu_        = &engine_.Get<ArmMmu>();
    translator_ = &engine_.Get<ArmTranslator>();
    cache_      = &engine_.Get<ArmTranslationCache>();
    compiler_   = &engine_.Get<ArmBlockCompiler>();
    channel_    = &engine_.Get<ArmInterruptChannel>();
    host_requests_ = &engine_.Get<HostRequestChannel>();
    host_       = &engine_.Host();
    entry_stub_ = engine_.Get<ArmIrCodegen>().EntryStub();
    engine_.AttachClock(clock_);
}

GuestEngine::RunStatus ArmJit::Run() {
    mmu_->SynchronizeSctlr();

    if (cpu_state_->reset_pending != 0u) {
        std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
            .fetch_and(~kChainExitReset, std::memory_order_acq_rel);
        host_->OnResetDelivered();
        cpu_->RaiseResetException();
        cache_->Flush();
        return RunStatus::kRunning;
    }

    if (channel_->TakeHostRequest()) host_requests_->ServiceRequests();
    host::ClockDispatch(clock_);

    /* QEMU cpu_handle_interrupt (cpu-exec.c): "Clear the interrupt
       flag now since we're processing cpu->interrupt_request and
       cpu->exit_request." */
    {
        std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
        const uint32_t            exits = word.load(std::memory_order_acquire);
        if ((exits & (kChainExitClock | kChainExitIdle)) != 0u) {
            if ((exits & kChainExitClock) != 0u) channel_->WaitForClock();
            else                                 channel_->ResumeIdle();
            return RunStatus::kRunning;
        }
        if ((exits & kChainExitIrq) != 0u) {
            word.fetch_and(~kChainExitIrq, std::memory_order_acq_rel);
        }
    }
    const ArmInterruptChannel::IrqGate gate = channel_->EvaluateGate();
    cpu_state_->irq_interrupt_pending = gate.level;
    if (gate.raise) {
        cpu_->RaiseIrqException(cpu_state_->gprs[ArmGpr::kR15]);
    }

    const uint32_t pc = cpu_state_->gprs[ArmGpr::kR15];
    const uint32_t folded = ArmFcseFold(pc, mmu_->State()->fcse_fold_id);
    void*          native =
        cache_->Lookup(cpu_state_->cpsr.bits.thumb_mode != 0u,
                       cpu_state_->cpsr.bits.mode == ArmMode::kUser ? kBlockUser : kBlockPrivileged,
                       folded);
    if (native == nullptr) {
        compiler_->SetPredecessor(predecessor_va_);
        void* compiled = nullptr;
        auto  compile  = [this, pc, &compiled] { compiled = compiler_->Compile(pc); };
        if (!platform::CallHaltGuarded(compile)) {
            return RunStatus::kHalted;
        }
        native = compiled;
        if (native == nullptr) {
            return RunStatus::kRunning;
        }
    }
    predecessor_va_ = folded;

#if CERFENGINE_BACKEND_X86_WIN32
    __try {
        cerfir::HostEnterBlock(native, cpu_state_, mmu_->State(), entry_stub_);
    } __except (platform::FilterDispatchException(_exception_info())) {
        const platform::HostFault& fault = platform::LastDispatchFault();
        if (fault.code != platform::kHaltUnwindCode) {
            ReportHostFault(fault.code, fault.address, fault.registers, pc);
        }
        return RunStatus::kHalted;
    }
    return RunStatus::kRunning;
#elif CERFENGINE_GUARDED_DISPATCH
    ArmBlockEntry       entry{native, cpu_state_, mmu_->State(), entry_stub_};
    platform::HostFault fault{};
    const uint32_t      code = platform::CallDispatchGuarded(&EnterArmBlock, &entry, fault);
    if (code == 0u) return RunStatus::kRunning;
    if (code == platform::kHaltUnwindCode) {
        platform::RethrowHostException();
        return RunStatus::kHalted;
    }
    if (code != platform::kHostFaultCode) {
        engine_.Panic("ArmJit: unwind code 0x%08X while running guest PC 0x%08X", code, pc);
    }
    ReportHostFault(fault.code, fault.address, fault.registers, pc);
#else
    engine_.Panic("ArmJit: guest PC 0x%08X reached dispatch in a build without an execution "
                  "backend", pc);
#endif
}

void ArmJit::SetHostChainExit(bool requested) {
    std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
    if (requested) {
        word.fetch_or(kChainExitHost, std::memory_order_acq_rel);
    } else {
        word.fetch_and(~kChainExitHost, std::memory_order_acq_rel);
    }
}

void ArmJit::SetInterruptPending()   { channel_->SetInterruptPending(); }
void ArmJit::ClearInterruptPending() { channel_->ClearInterruptPending(); }
void ArmJit::SetIdleWake(bool level) { channel_->SetIdleWake(level); }

void ArmJit::EnterDeepSleep() {
    /* SA-1110 §9.5.3: PMCR.SF halts the CPU until a wake reset. */
    cpu_state_->deep_sleep = 1;
}

void ArmJit::ExitDeepSleep() {
    cpu_state_->deep_sleep = 0;
    channel_->Wake();
}

void ArmJit::EnterIdleWait() { channel_->EnterIdle(); }

void ArmJit::SetCoreClockStopped(bool stopped) {
    channel_->SetClockStopped(stopped);
    std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
    if (stopped) {
        word.fetch_or(kChainExitClock, std::memory_order_acq_rel);
    } else {
        word.fetch_and(~kChainExitClock, std::memory_order_acq_rel);
    }
}

void ArmJit::SetResetPending(bool is_resume) {
    host_->OnResetRequested(is_resume);
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kChainExitReset, std::memory_order_acq_rel);
    cpu_state_->reset_pending = 1u;
    channel_->Wake();
    if (is_resume) return;
    host_->OnRebootRequested();
}

void ArmJit::PrintFatalDump() {
    const auto& r = cpu_state_->gprs;
    CERFENGINE_LOG(engine_, kCaution, "      guest PC=0x%08X  CPSR=0x%08X\n",
                   r[ArmGpr::kR15], ArmPackCpsr(*cpu_state_));
    CERFENGINE_LOG(engine_, kCaution, "      R0=0x%08X  R1=0x%08X  R2=0x%08X  R3=0x%08X "
                   "R4=0x%08X  R5=0x%08X  R6=0x%08X  R7=0x%08X\n",
                   r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    CERFENGINE_LOG(engine_, kCaution, "      R8=0x%08X  R9=0x%08X  R10=0x%08X R11=0x%08X "
                   "R12=0x%08X SP=0x%08X  LR=0x%08X\n",
                   r[8], r[9], r[10], r[11], r[12], r[13], r[14]);
}

void ArmJit::FlushTranslationCache() { cache_->Flush(); }

void ArmJit::SetDmaRegion(uint32_t, uint32_t) {}

std::optional<uint8_t*> ArmJit::PeekGuestVa(uint32_t va) {
    return translator_->PeekDataTlb(va);
}

uint8_t* ArmJit::ResolveGuestVaToHost(uint32_t va) {
    return translator_->PeekVaToHost(va);
}

bool ArmJit::ResolveGuestVaToPa(uint32_t va, uint32_t* pa) {
    return translator_->PeekVaToPa(va, pa);
}

}
