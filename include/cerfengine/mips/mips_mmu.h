#pragma once

#include "../calling_convention.h"
#include <cstdint>

#include "../engine.h"
#include "../force_inline.h"
#include "../isa_block_space.h"
#include "../unmapped_windows.h"
#include "mips_cpu_state.h"
#include "mips_system_host.h"
#include "mips_tlb_context.h"

namespace cerfengine {

namespace MipsSeg {
    constexpr uint32_t kKusegEnd     = UnmappedWindows::kCachedBase;
    constexpr uint32_t kKseg1Base    = UnmappedWindows::kUncachedBase;
    constexpr uint32_t kKseg2Base    = UnmappedWindows::kEnd;
    constexpr uint32_t kSsegEnd      = 0xE0000000;
    constexpr uint32_t kUnmappedMask = UnmappedWindows::kPaMask;

    constexpr bool IsUnmapped(uint32_t va) { return UnmappedWindows::Contains(va); }
    constexpr uint32_t UnmappedPa(uint32_t va) { return UnmappedWindows::Pa(va); }
}

class MipsMmu : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    void Bind(IsaBlockSpace* blocks32, IsaBlockSpace* blocks16) {
        context_.Bind(state_, blocks32, blocks16);
    }

    /* VR4121 UM §6.2.5 Table 6-1 (p.162), §6.2.6 Table 6-2 (p.164); TMPR39xx-um §5.1
       (p.43): User mode reaches only 0x00000000-0x7FFFFFFF, Supervisor mode adds
       0xC0000000-0xDFFFFFFF, any other address raises an Address Error. */
    template <uint32_t kPrivilege>
    CERFENGINE_FORCEINLINE MipsTlbResult Translate(MipsCpuState* st, uint32_t va, MipsAccess acc,
                                                   uint32_t* pa) {
        if (fixed_window_size_ != 0u && va - fixed_window_va_ < fixed_window_size_) {
            *pa = fixed_window_pa_ + (va - fixed_window_va_);
            return MipsTlbResult::kMatch;
        }
        if constexpr (kPrivilege == kBlockPrivileged) {
            if (MipsSeg::IsUnmapped(va)) {
                *pa = MipsSeg::UnmappedPa(va);
                return MipsTlbResult::kMatch;
            }
        } else if constexpr (kPrivilege == kBlockUser) {
            if (va >= MipsSeg::kKusegEnd) return MipsTlbResult::kAddressError;
        } else {
            static_assert(kPrivilege == kBlockSupervisor);
            if (va >= MipsSeg::kKusegEnd &&
                va - MipsSeg::kKseg2Base >= MipsSeg::kSsegEnd - MipsSeg::kKseg2Base) {
                return MipsTlbResult::kAddressError;
            }
        }
        return map_(map_context_, st, va, acc, pa);
    }

    CERFENGINE_FORCEINLINE MipsTlbResult TranslateAs(uint32_t privilege, MipsCpuState* st,
                                                     uint32_t va, MipsAccess acc, uint32_t* pa) {
        if (privilege == kBlockPrivileged) return Translate<kBlockPrivileged>(st, va, acc, pa);
        return TranslateUnprivileged(privilege, st, va, acc, pa);
    }

    MipsTlbResult TranslateUnprivileged(uint32_t privilege, MipsCpuState* st, uint32_t va,
                                        MipsAccess acc, uint32_t* pa);

    bool ExecPageGlobal(MipsCpuState* st, uint32_t va) {
        if (MipsSeg::IsUnmapped(va)) {
            return true;
        }
        return tlb_.page_global(tlb_.context, st, va);
    }

    void SetFixedWindow(uint32_t va, uint32_t pa, uint32_t size) {
        fixed_window_va_   = va;
        fixed_window_pa_   = pa;
        fixed_window_size_ = size;
    }

    cerfengine::MipsTlbContext& TlbContext() { return context_; }

    uint32_t ErlMask() const { return erl_mask_; }

    void ArmErlMap();

    static void CERFENGINE_FASTCALL ErlSetHelper(MipsMmu* mmu);
    static void CERFENGINE_FASTCALL TlbWriteIndexedHelper(MipsMmu* mmu);
    static void CERFENGINE_FASTCALL TlbWriteRandomHelper(MipsMmu* mmu);
    static void CERFENGINE_FASTCALL TlbProbeHelper(MipsMmu* mmu);
    static void CERFENGINE_FASTCALL TlbReadHelper(MipsMmu* mmu);

private:
    static MipsTlbResult CERFENGINE_FASTCALL ErlMapAddress(void* context, MipsCpuState* st,
                                                           uint32_t va, MipsAccess acc,
                                                           uint32_t* pa);
    void LeaveErlMode();
    [[noreturn]] void ErlKusegFetch(uint32_t va);

    cerfengine::MipsTlbHooks   tlb_;
    MipsMapAddressFn           map_         = nullptr;
    void*                      map_context_ = nullptr;
    MipsCpuState*              state_ = nullptr;
    uint32_t                   erl_mask_ = 0;
    cerfengine::MipsTlbContext context_;
    uint32_t fixed_window_va_   = 0;
    uint32_t fixed_window_pa_   = 0;
    uint32_t fixed_window_size_ = 0;
};

}
