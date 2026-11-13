#include <cerfengine/guest_engine.h>

#include "../cerfir/cerfir_host_lowering.h"
#include "../guarded_block_entry.h"
#include "../host_platform.h"
#include <cerfengine/physical_map.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_interrupt_channel.h>
#include <cerfengine/sh/sh_mmu.h>
#include <cerfengine/unmapped_windows.h>
#include "sh_address_space.h"
#include "sh_block_compiler.h"
#include "sh_memory_access.h"
#include "sh_translation_cache.h"

#include <atomic>
#include <cstdint>
#include <optional>

namespace cerfengine {

namespace {

class ShJit : public GuestEngine {
public:
    using GuestEngine::GuestEngine;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }

    void OnReady() override {
        cpu_         = &engine_.Get<ShCpu>();
        cpu_state_   = cpu_->State();
        cache_       = &engine_.Get<ShTranslationCache>();
        host_        = &engine_.Host();
        compiler_    = &engine_.Get<ShBlockCompiler>();
        map_         = &engine_.Get<PhysicalMap>();
        block_entry_ = &engine_.Get<GuardedBlockEntry>();
        mem_access_  = &engine_.Get<ShMemoryAccess>();
        interrupts_  = &engine_.Get<ShInterruptChannel>();
        mmu_         = &engine_.Get<ShMmu>();
    }

    RunStatus Run() override {
        const uint32_t stops =
            std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request).load(std::memory_order_relaxed);
        if ((stops & (kShChainExitSleep | kShChainExitReset)) != 0u) {
            if ((stops & kShChainExitReset) != 0u) {
                DeliverReset();
                return RunStatus::kRunning;
            }
            bool woke  = false;
            auto sleep = [this, &woke] { woke = interrupts_->Sleep(); };
            if (!platform::CallHaltGuarded(sleep)) return RunStatus::kHalted;
            if (!woke) return RunStatus::kRunning;
        }
        mem_access_->DeliverDueClockEvents();
        interrupts_->DeliverPending();
        const uint32_t pc     = cpu_state_->pc;
        void*          native = compiler_->FindBlockNativeStart(pc);
        if (native == nullptr) {
            compiler_->SetPredecessor(predecessor_va_);
            auto compile = [this, pc, &native] { native = compiler_->Compile(pc); };
            if (!platform::CallHaltGuarded(compile)) return RunStatus::kHalted;
            if (native == nullptr) return RunStatus::kRunning;
        }
        predecessor_va_ = pc;

#if CERFENGINE_BACKEND_X86_WIN32
        __try {
            cerfir::HostEnterBlock(native, cpu_state_);
        } __except (platform::FilterDispatchException(_exception_info())) {
            const platform::HostFault& fault = platform::LastDispatchFault();
            if (fault.code == platform::kHaltUnwindCode) return RunStatus::kHalted;
            if (fault.code != platform::kGuestUnwindCode) block_entry_->ReportHostFault("ShJit", fault, pc);
            mem_access_->EndMmioAccess();
        }
        return RunStatus::kRunning;
#elif CERFENGINE_GUARDED_DISPATCH
        auto                enter = [&] { cerfir::HostEnterBlock(native, cpu_state_); };
        platform::HostFault fault{};
        switch (block_entry_->Enter(enter, pc, fault)) {
        case BlockExit::kReturned:
            return RunStatus::kRunning;
        case BlockExit::kGuestUnwind:
            mem_access_->EndMmioAccess();
            return RunStatus::kRunning;
        case BlockExit::kHalted:
            return RunStatus::kHalted;
        case BlockExit::kHostFault:
            break;
        }
        block_entry_->ReportHostFault("ShJit", fault, pc);
#else
        block_entry_->PanicWithoutBackend(pc);
#endif
    }

    void SetHostChainExit(bool requested) override { interrupts_->SetHostExit(requested); }

    bool     DeepSleep()    const override { return cpu_state_->deep_sleep != 0u; }
    bool     ResetPending() const override { return interrupts_->ResetRequested(); }
    uint32_t Pc()           const override { return cpu_state_->pc; }
    uint32_t PhysAddrMask() const override { return UnmappedWindows::kPaMask; }

    void PrintFatalDump() override {
        const ShCpuState& s = *cpu_state_;
        CERFENGINE_LOG(engine_, kCaution, "      guest PC=0x%08X  SR=0x%08X  PR=0x%08X  GBR=0x%08X  VBR=0x%08X\n",
                       s.pc, s.sr, s.pr, s.gbr, s.vbr);
        CERFENGINE_LOG(engine_, kCaution, "      MACH=0x%08X  MACL=0x%08X  SSR=0x%08X  SPC=0x%08X\n",
                       s.mach, s.macl, s.ssr, s.spc);
        for (uint32_t i = 0; i < kShNumGpr; i += 4) {
            CERFENGINE_LOG(engine_, kCaution, "      r%-2u=0x%08X  r%-2u=0x%08X  r%-2u=0x%08X  r%-2u=0x%08X\n",
                           i, s.gpr[i], i + 1, s.gpr[i + 1], i + 2, s.gpr[i + 2], i + 3, s.gpr[i + 3]);
        }
    }

    std::optional<uint8_t*> PeekGuestVa(uint32_t va) override {
        uint32_t pa = 0u;
        if (!HostPa(va, &pa)) return std::nullopt;
        uint8_t* host = map_->PeekRead(pa);
        if (!host) return std::nullopt;
        return host;
    }

    uint8_t* ResolveGuestVaToHost(uint32_t va) override {
        uint32_t pa = 0u;
        if (!HostPa(va, &pa)) return nullptr;
        uint8_t* w = map_->PeekWrite(pa);
        return w ? w : map_->PeekRead(pa);
    }

    bool ResolveGuestVaToPa(uint32_t va, uint32_t* pa) override { return HostPa(va, pa); }

    bool GuestIrqMasked() const override {
        engine_.Panic("ShJit: the SH interrupt mask query is not modeled");
    }

    void FlushTranslationCache() override { cache_->Flush(); }

    void SetResetPending(bool is_resume) override {
        host_->OnResetRequested(is_resume);
        if (!is_resume) host_->OnRebootRequested();
        interrupts_->RequestReset();
    }

    void EnterDeepSleep() override { cpu_state_->deep_sleep = 1u; }

    void ExitDeepSleep() override { cpu_state_->deep_sleep = 0u; }

    void EnterIdleWait() override { engine_.Panic("ShJit: an idle wait is not modeled"); }

    void SetCoreClockStopped(bool stopped) override {
        engine_.Panic("ShJit: a core clock %s request is not modeled", stopped ? "stop" : "restart");
    }

    void SetDmaRegion(uint32_t pa, uint32_t size) override {
        engine_.Panic("ShJit: a DMA region PA 0x%08X size 0x%X is not modeled", pa, size);
    }

private:
    void DeliverReset() {
        interrupts_->ClearResetRequest();
        cpu_->ResetState(cpu_->TakePendingResetKind());
        host_->OnResetDelivered();
    }

    bool HostPa(uint32_t va, uint32_t* pa) {
        if (ShAddressSpace::DirectlyMapped(va) || (ShAddressSpace::Paged(va) && !mmu_->Translates(va))) {
            *pa = ShAddressSpace::Pa(va);
            return true;
        }
        if (!ShAddressSpace::Paged(va)) {
            engine_.Panic("ShJit: host lookup of VA 0x%08X in P4 outside the on-chip I/O space", va);
        }
        ShTranslation t{};
        if (!mmu_->Probe(va, ShAccess::kRead, &t)) return false;
        *pa = t.pa;
        return true;
    }

    ShCpu*              cpu_            = nullptr;
    ShCpuState*         cpu_state_      = nullptr;
    ShTranslationCache* cache_          = nullptr;
    EngineHost*         host_           = nullptr;
    ShBlockCompiler*    compiler_       = nullptr;
    PhysicalMap*        map_            = nullptr;
    GuardedBlockEntry*  block_entry_    = nullptr;
    ShMemoryAccess*     mem_access_     = nullptr;
    ShInterruptChannel* interrupts_     = nullptr;
    ShMmu*              mmu_            = nullptr;
    uint32_t            predecessor_va_ = 0;
};

}

CERFENGINE_REGISTER_AS(ShJit, GuestEngine);

}
